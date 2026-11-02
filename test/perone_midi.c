#include "support.h"
#include "script.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void messages(void) {
	Plugin p = {0};
	assert(!open_bundle(&p, "build/test/midi.perone", 48000));
	assert(!p.config.input && !p.config.output && p.config.midi == 1 && p.config.midi_output == 1);
	MidiBuffer in = {0}, out = {0};
	assert(!midi_add(&in, 3, (uint8_t[]){0x92, 60, 100}, 3));
	assert(!midi_add(&in, 5, (uint8_t[]){0xc2, 12}, 2));
	assert(!midi_add(&in, 5, (uint8_t[]){0xf8}, 1));
	assert(!midi_add(&in, 17, (uint8_t[]){0x82, 60, 0}, 3));
	assert(!render_plugin(&p, NULL, NULL, 31, &in, &out));
	assert(out.count == 4 && out.used == 9);
	const unsigned frames[] = {10, 12, 12, 24}, sizes[] = {3, 2, 1, 3};
	const uint8_t bytes[] = {0x92, 72, 100, 0xc2, 12, 0xf8, 0x82, 72, 0};
	for (unsigned i = 0; i < 4; ++i)
		assert(out.events[i].frame == frames[i] && out.events[i].size == sizes[i]);
	assert(!memcmp(out.data, bytes, sizeof(bytes)));
	// An event exactly at frames belongs to the next process call.
	midi_dsp(p.dsp, 1, (uint8_t[]){0x90, 60, 100}, 3);
	assert(!process_dsp(p.dsp, NULL, NULL, 0, NULL) && !midi_output_dsp(p.dsp)->count);
	assert(!process_dsp(p.dsp, NULL, NULL, 7, NULL) && !midi_output_dsp(p.dsp)->count);
	assert(!process_dsp(p.dsp, NULL, NULL, 1, NULL));
	assert(midi_output_dsp(p.dsp)->count == 1 && !midi_output_dsp(p.dsp)->events[0].frame);
	for (int fault = 1; fault <= 4; ++fault) {
		set_dsp(p.dsp, 2, fault);
		assert(process_dsp(p.dsp, NULL, NULL, 8, NULL));
		set_dsp(p.dsp, 2, 0);
		reset_dsp(p.dsp);
		assert(!process_dsp(p.dsp, NULL, NULL, 8, NULL) && !midi_output_dsp(p.dsp)->count);
	}
	close_plugin(&p);
}

static void graph(unsigned rate, unsigned chunk) {
	Session s = {.sample_rate = rate};
	Output output;
	assert(!prepare_session(&s, &output, "test/perone-midi.janet", NULL, NULL, NULL));
	atomic_store(&s.midi_capture[2], 1);
	float audio[BLOCK * 2];
	MidiPacket packet;
	unsigned count = 0;
	const double times[] = {.001, .004, .009, .011, .015};
	const uint8_t data[][3] = {{0x90, 72, 100}, {0x80, 72, 0}, {0xc2, 12, 0}, {0x90, 53, 90}, {0x80, 53, 0}};
	while (s.time < s.frames) {
		size_t n = s.frames - s.time < chunk ? s.frames - s.time : chunk;
		assert(!session_render(&s, audio, n));
		while (midi_pop(s.midi_out, &packet)) {
			assert(count < 5 && packet.node == 2);
			assert(packet.frame == (uint64_t)llround(times[count] * rate) + (count < 3 ? 18 : 24));
			assert(packet.size == (count == 2 ? 2u : 3u) && !memcmp(packet.data, data[count], packet.size));
			++count;
		}
	}
	assert(count == 5);
	// The same MIDI-only chain also handles hardware input while stopped.
	assert(!session_seek(&s, 0));
	uint64_t clock = s.clock;
	assert(!session_midi(&s, 0, (uint8_t[]){0x91, 60, 100}, 3));
	assert(!session_monitor(&s, audio, 128));
	assert(midi_pop(s.midi_out, &packet) && packet.frame == clock + 18 && packet.data[1] == 72);
	assert(!midi_pop(s.midi_out, &packet));
	session_free(&s);
}

int main(void) {
	messages();
	for (unsigned rate = 44100; rate <= 48000; rate += 3900)
		for (unsigned chunk = 1; chunk <= 1024; chunk = chunk * 3 + 1)
			graph(rate, chunk);
	puts(
	    "OK: Perone MIDI-only processing, bus indices, short messages, offsets, automation, live input, ABI 2 instruments and overflow recovery");
	return 0;
}
