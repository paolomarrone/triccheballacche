#include "support.h"
#include "snapshot.h"
#include "host/dsp_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void buffers(void) {
	MidiBuffer a = {0}, b = {0};
	const uint8_t on[] = {0x92, 60, 100}, cc[] = {0xb2, 7, 91}, sysex[] = {0xf0, 0x7d, 1, 2, 0xf7};
	assert(midi_size(0xc0) == 2 && midi_size(0xf8) == 1 && !midi_size(0xf4));
	assert(!midi_valid((uint8_t[]){0x90, 128, 100}, 3) && !midi_valid(sysex, 4));
	assert(!midi_add(&a, 12, on, 3) && !midi_add(&a, 4, cc, 3) && !midi_add(&b, 0, sysex, 5));
	assert(!midi_merge(&a, &b, 4) && a.events[0].frame == 4 && a.events[1].frame == 4 && a.events[2].frame == 12);
	assert(a.data[a.events[0].offset] == 0xb2 && a.data[a.events[1].offset] == 0xf0);
	MidiQueue *q = calloc(1, sizeof(*q));
	assert(q);
	MidiPacket p = {.node = 7, .frame = 1234, .size = 3, .data = {0x90, 60, 100}}, out;
	for (unsigned i = 0; i < MIDI_EVENTS; ++i)
		assert(!midi_push(q, &p));
	assert(midi_push(q, &p) && atomic_load(&q->failed));
	for (unsigned i = 0; i < MIDI_EVENTS; ++i)
		assert(midi_pop(q, &out) && out.node == 7 && out.frame == 1234);
	assert(!midi_pop(q, &out));
	free(q);
}
static void streams(void) {
	Session s = {.sample_rate = 48000};
	Output output;
	ScoreView view = {0};
	const char *source = "(def a (daw/midi :keyboard)) (def b (daw/midi :merge))"
	                     "(daw/midi-through a b) (daw/midi-output b)"
	                     "(daw/note a 0.01 0.04 60 100 3)"
	                     "(daw/note a 0.02 0.06 60 70 4)"
	                     "(daw/midi-event a 0.03 [179 7 93]) (daw/midi-event a 0.035 [195 12])"
	                     "(daw/end 0.1)";
	assert(!prepare_session(&s, &output, "test/midi.janet", source, NULL, &view));
	assert(view.nodes[0].count == 4 && view.nodes[1].nmidi_inputs == 1);
	atomic_store(&s.midi_capture[1], 1);
	float audio[BLOCK * 2];
	MidiPacket p;
	int notes = 0, controls = 0;
	while (s.time < s.frames) {
		size_t n = s.frames - s.time < 37 ? s.frames - s.time : 37;
		assert(!session_render(&s, audio, n));
		for (size_t i = 0; i < 2 * n; ++i)
			assert(audio[i] == 0);
		while (midi_pop(s.midi_out, &p)) {
			if (p.data[0] == 0x93) {
				assert(p.frame == 480);
				++notes;
			} else if (p.data[0] == 0x94) {
				assert(p.frame == 960);
				++notes;
			} else if (p.data[0] == 0x83) {
				assert(p.frame == 2400);
				++notes;
			} else if (p.data[0] == 0x84) {
				assert(p.frame == 3840);
				++notes;
			} else
				++controls;
		}
	}
	assert(notes == 4 && controls == 2);
	assert(!session_seek(&s, 1920) && !session_render(&s, audio, 1));
	assert(midi_pop(s.midi_out, &p) && p.data[0] == 0xb3 && p.data[2] == 93);
	assert(midi_pop(s.midi_out, &p) && p.data[0] == 0xc3 && p.size == 2);
	assert(midi_pop(s.midi_out, &p) && p.data[0] == 0x93);
	assert(midi_pop(s.midi_out, &p) && p.data[0] == 0x94);
	assert(!midi_pop(s.midi_out, &p));
	assert(!session_midi(&s, 0, (uint8_t[]){0x92, 65, 95}, 3));
	assert(!session_render(&s, audio, 1));
	assert(midi_pop(s.midi_out, &p) && p.data[0] == 0x92);
	session_midi_panic(&s);
	assert(!session_render(&s, audio, 1));
	int off = 0;
	while (midi_pop(s.midi_out, &p)) {
		assert((p.data[0] & 15) == 2); // Panic must not release the score's other channels.
		off += p.data[0] == 0x82 && p.data[1] == 65;
	}
	assert(off == 1);
	session_midi_panic(&s);
	assert(!session_render(&s, audio, 1) && !midi_pop(s.midi_out, &p));
	session_free(&s);
	score_view_free(&view);
}
// The same graph handles devices at rest, score events in Play, and tails after Stop.
static void monitoring(void) {
	Session s = {.sample_rate = 48000};
	Output output;
	const char *source = "(def keys (daw/midi :keys)) (daw/midi-output keys)"
	                     "(daw/note keys 0 1 60 80) (daw/midi-event keys 0 [0xb0 64 127]) (daw/end 2)";
	assert(!prepare_session(&s, &output, "test/monitor.janet", source, NULL, NULL));
	atomic_store(&s.midi_capture[0], 1);
	float audio[BLOCK * 2];
	MidiPacket p;
	assert(!session_monitor(&s, audio, 128) && !s.time && s.clock == 128);
	assert(!midi_pop(s.midi_out, &p)); // No scheduled events or automation while stopped.
	assert(!session_midi(&s, 0, (uint8_t[]){0x92, 67, 95}, 3));
	assert(!session_monitor(&s, audio, 128) && !s.time && s.clock == 256);
	assert(midi_pop(s.midi_out, &p) && p.frame == 128 && p.data[0] == 0x92);
	assert(!session_render(&s, audio, 128) && s.time == 128 && s.clock == 384);
	assert(midi_pop(s.midi_out, &p) && p.frame == 256 && p.data[0] == 0xb0 && p.data[2] == 127);
	assert(midi_pop(s.midi_out, &p) && p.frame == 256 && p.data[0] == 0x90);
	assert(!session_monitor(&s, audio, 128) && s.time == 128 && s.clock == 512);
	assert(midi_pop(s.midi_out, &p) && p.frame == 384 && p.data[0] == 0xb0 && p.data[2] == 0);
	assert(midi_pop(s.midi_out, &p) && p.frame == 384 && p.data[0] == 0x80);
	assert(!midi_pop(s.midi_out, &p)); // Stop leaves the keyboard's note held.
	assert(!session_render(&s, audio, 128) && s.time == 256);
	assert(midi_pop(s.midi_out, &p) && p.data[0] == 0xb0 && p.data[2] == 127);
	assert(midi_pop(s.midi_out, &p) && p.data[0] == 0x90 && p.data[2] == 80);
	assert(!session_seek(&s, 24000) && !session_monitor(&s, audio, 128) && s.time == 24000);
	assert(s.clock == 768);            // Seeking never rewinds the device clock.
	assert(!midi_pop(s.midi_out, &p)); // Seeking at rest must not send score note-ons or sustain.
	assert(!session_render(&s, audio, 1));
	assert(midi_pop(s.midi_out, &p) && p.frame == 768 && p.data[0] == 0xb0 && p.data[2] == 127);
	assert(midi_pop(s.midi_out, &p) && p.frame == 768 && p.data[0] == 0x90);
	session_free(&s);

	// A graph with no musical sequence needs neither a duration nor a dummy loop.
	const char *graph = "(def p (daw/plugin \"build/test/fixture.perone\" {:gain 0.25 :mode 2}))"
	                    "(daw/output (daw/through p (daw/plugin \"build/test/effect.perone\" {:gain 0.5})))";
	assert(!prepare_session(&s, &output, "test/monitor.janet", graph, NULL, NULL));
	assert(!s.frames && !session_monitor(&s, audio, 128) && !s.time);
	assert(audio[0] == .125f && audio[1] == -.125f); // Transport is stopped, even through stereo effects.
	assert(!session_midi(&s, 0, (uint8_t[]){0x80, 60, 0}, 3));
	assert(!session_monitor(&s, audio, 128) && audio[0] == 0 && audio[1] == 0);
	assert(!session_midi(&s, 0, (uint8_t[]){0x90, 60, 100}, 3));
	edit_dsp(s.nodes[0].dsp[0].dsp, 1, .5);
	assert(!session_monitor(&s, audio, 128) && audio[0] == .25f && audio[1] == -.25f);
	assert(!s.time && s.clock == 384);
	edit_dsp(s.nodes[1].dsp[0].dsp, 2, .1);
	assert(!session_monitor(&s, audio, 128));
	assert(!session_midi(&s, 0, (uint8_t[]){0x80, 60, 0}, 3));
	assert(!session_monitor(&s, audio, 32));
	assert(audio[0] < -.2f && fabsf(audio[62]) < fabsf(audio[0])); // Effect state decays while stopped.
	session_free(&s);
	puts("OK: stopped graph, live MIDI and parameters, score-note release/resume, sustain and independent audio clock");
}
static void resume_order(void) {
	Session s = {.sample_rate = 48000};
	Output output;
	const char *source = "(def keys (daw/midi)) (daw/midi-output keys)"
	                     "(daw/note keys 0 1 72 90) (daw/note keys 0.01 1 60 80)"
	                     "(daw/note keys 0.02 1 72 100) (daw/end 2)";
	assert(!prepare_session(&s, &output, "test/monitor.janet", source, NULL, NULL));
	atomic_store(&s.midi_capture[0], 1);
	float audio[256];
	MidiPacket p;
	for (int seek = 0; seek < 2; ++seek) {
		if (seek)
			assert(!session_seek(&s, 1440));
		else
			while (s.time < 1440) {
				assert(!session_render(&s, audio, 128));
				while (midi_pop(s.midi_out, &p)) {
				}
			}
		assert(!session_monitor(&s, audio, 128));
		while (midi_pop(s.midi_out, &p))
			assert((p.data[0] & 0xf0) == 0x80);
		assert(!session_render(&s, audio, 1));
		assert(midi_pop(s.midi_out, &p) && p.data[1] == 60 && p.data[2] == 80);
		assert(midi_pop(s.midi_out, &p) && p.data[1] == 72 && p.data[2] == 100);
		assert(!midi_pop(s.midi_out, &p));
	}
	session_free(&s);
}
static void formats(unsigned chunk) {
	Session s = {.sample_rate = 48000};
	Output output;
	const char *source =
	    "(def events (daw/plugin {:format :lv2 :path \"build/test/fixture.lv2\" :id \"urn:triccheballacche:test:lv2:midi-output\"}))"
	    "(def a (daw/plugin {:format :clap :path \"build/test/fixture.clap\" :id \"org.triccheballacche.test.synth\"}))"
	    "(def b (daw/plugin {:format :vst3 :path \"build/test/fixture.vst3\" :id \"ABCDEF01222233334444555566667777\"}))"
	    "(def c (daw/plugin \"build/test/fixture.perone\"))"
	    "(daw/midi-through events a) (daw/midi-through a b) (daw/midi-through b c)"
	    "(daw/output (daw/track c)) (daw/end 0.005)";
	assert(!prepare_session(&s, &output, "test/midi.janet", source, NULL, NULL));
	atomic_store(&s.midi_capture[2], 1);
	float audio[BLOCK * 2];
	MidiPacket p;
	int count = 0;
	while (s.time < s.frames) {
		size_t n = s.frames - s.time < chunk ? s.frames - s.time : chunk;
		assert(!session_render(&s, audio, n));
		while (midi_pop(s.midi_out, &p)) {
			assert(p.frame == (count ? 101 : 37) && p.data[0] == (count ? 0x80 : 0x90) && p.data[1] == 67);
			++count;
		}
	}
	assert(count == 2);
	uint64_t clock = s.clock;
	assert(!session_seek(&s, 0) && !session_monitor(&s, audio, 128) && !s.time);
	assert(midi_pop(s.midi_out, &p) && p.frame == clock + 37 && p.data[0] == 0x90);
	assert(midi_pop(s.midi_out, &p) && p.frame == clock + 101 && p.data[0] == 0x80);
	assert(!midi_pop(s.midi_out, &p));
	session_free(&s);
}
static void rejection(void) {
	Score s = {0};
	int a = score_midi(&s), b = score_midi(&s);
	assert(score_midi_through(&s, a, b) == b);
	assert(score_midi_through(&s, a, b) < 0);
	assert(score_midi_through(&s, b, a) == a && score_midi_output(&s, b) == b);
	assert(score_end(&s, 100) && strstr(s.error, "cycle"));
	score_free(&s);
}
static void messages(void) {
	assert(!setenv("CLAP_TEST_MIDI", "1", 1));
	Session s = {.sample_rate = 48000};
	Output output;
	const char *source =
	    "(def a (daw/plugin {:format :clap :path \"build/test/fixture.clap\" :id \"org.triccheballacche.test.synth\"}))"
	    "(def b (daw/plugin {:format :vst3 :path \"build/test/fixture.vst3\" :id \"ABCDEF01222233334444555566667777\"}))"
	    "(daw/midi-through a b) (daw/midi-output b) (daw/end 0.1)";
	assert(!prepare_session(&s, &output, "test/midi.janet", source, NULL, NULL));
	assert(!unsetenv("CLAP_TEST_MIDI"));
	atomic_store(&s.midi_capture[0], 1);
	atomic_store(&s.midi_capture[1], 1);
	const uint8_t sysex[] = {0xf0, 0x7d, 1, 2, 3, 4, 5, 0xf7};
	assert(!session_midi(&s, 0, (uint8_t[]){0x93, 65, 90}, 3));
	assert(!session_midi(&s, 0, sysex, sizeof(sysex)));
	assert(!session_midi(&s, 0, (uint8_t[]){0xb3, 1, 100}, 3));
	float audio[2];
	assert(!session_render(&s, audio, 1));
	MidiPacket p;
	int notes = 0, controls = 0, system = 0;
	while (midi_pop(s.midi_out, &p)) {
		assert(!p.frame);
		if (p.data[0] == 0xf0) {
			assert(p.size == sizeof(sysex) && !memcmp(p.data, sysex, sizeof(sysex)));
			++system;
		} else if (p.data[0] == 0x93) {
			assert(p.data[1] == 65 && p.data[2] == 90);
			++notes;
		} else {
			assert(!p.node && p.data[0] == 0xb3 && p.data[1] == 1 && p.data[2] == 100);
			++controls;
		}
	}
	assert(notes == 2 && system == 2 && controls == 1);
	session_free(&s);
}
static void branches(void) {
	Session s = {.sample_rate = 48000};
	Output output;
	const char *source =
	    "(def ref {:format :lv2 :path \"build/test/fixture.lv2\" :id \"urn:triccheballacche:test:lv2:midi-output\"})"
	    "(def a (daw/plugin ref)) (def b (daw/plugin ref))"
	    "(def merge (daw/midi)) (def left (daw/midi)) (def right (daw/midi))"
	    "(daw/midi-through a merge) (daw/midi-through b merge)"
	    "(daw/midi-through merge left) (daw/midi-through merge right)"
	    "(daw/midi-output left) (daw/midi-output right) (daw/end 0.1)";
	assert(!prepare_session(&s, &output, "test/midi.janet", source, NULL, NULL));
	for (int i = 0; i < s.nnodes; ++i)
		atomic_store(&s.midi_capture[i], 1);
	float audio[256];
	assert(!session_render(&s, audio, 128));
	MidiPacket p;
	unsigned count = 0, nodes[5] = {0};
	while (midi_pop(s.midi_out, &p)) {
		assert(p.frame == (count < 8 ? 37 : 101)); // Device output is globally ordered, not grouped by node.
		assert(p.data[0] == (count < 8 ? 0x90 : 0x80));
		++nodes[p.node];
		++count;
	}
	assert(count == 16 && nodes[0] == 2 && nodes[1] == 2 && nodes[2] == 4 && nodes[3] == 4 && nodes[4] == 4);
	session_free(&s);
}
int main(void) {
	buffers();
	monitoring();
	resume_order();
	streams();
	rejection();
	messages();
	branches();
	for (unsigned chunk = 1; chunk < 512; chunk = chunk * 3 + 1)
		formats(chunk);
	assert(!setenv("CLAP_TEST_MIXED", "1", 1));
	formats(128);
	assert(!unsetenv("CLAP_TEST_MIXED"));
	puts(
	    "OK: MIDI channels, controllers, seek, external queues, panic, graph cycles and LV2 → CLAP → VST3 → Perone timestamps");
	return 0;
}
