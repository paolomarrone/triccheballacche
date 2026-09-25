#include "daw.h"
#include "snapshot.h"
#include "posix/module.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *source = "(import ../lib/pattern :as p)\n"
                            "(def tone (daw/plugin :tone \"build/test/fixture.perone\" {:gain 0.25}))\n"
                            "(daw/output (daw/track tone))\n"
                            "(daw/tempo 120)\n"
                            "(daw/score (p/parallel [(p/loop (p/map |[:note tone $ 100] (p/steps 1 [60 64])))\n"
                            " (p/loop (p/map |[:param tone :gain $] (p/events 3 [[0 0 0.2] [1 1 0.6]])))]) %s)";

static void prepare(Session *s, ScoreView *view, int finite, int changed) {
	char code[4096];
	snprintf(code, sizeof(code), source, finite ? "{:duration 4}" : "{}");
	if (changed) {
		char *at = strstr(code, "0.2]");
		assert(at);
		at[2] = '8';
	}
	Output cfg;
	char *diagnostics;
	int result = prepare_score(s, &cfg, "test/live-test.janet", code, &diagnostics, view);
	if (result)
		fprintf(stderr, "%s\n", diagnostics ? diagnostics : s->error);
	assert(!result && !diagnostics);
}

static void advance(Session *s, uint64_t until) {
	float out[1024];
	while (s->time < until) {
		size_t n = until - s->time < 512 ? until - s->time : 512;
		assert(!session_render(s, out, n));
	}
}

static void test_snapshot(void) {
	Session a = {.sample_rate = 48000, .describe = 1}, b = {0};
	ScoreView av, bv = {0};
	Output output = {0}, restored;
	prepare(&a, &av, 0, 0);
	size_t size, other_size;
	void *data = score_pack(&a, &output, &av, &size);
	assert(data && size);
	assert(!score_unpack(&b, &restored, &bv, data, size));
	assert(!a.audio && !a.held && !a.nodes[0].dsp[0].dsp);
	assert(!b.audio && !b.held && !b.nodes[0].dsp[0].dsp);
	void *other = score_pack(&b, &restored, &bv, &other_size);
	assert(other && other_size == size && !memcmp(data, other, size));
	free(other);
	assert(!session_activate(&b));
	assert(!session_activate(&a));
	float x[256], y[256];
	for (int i = 0; i < 100; ++i) {
		assert(!session_render(&a, x, 128) && !session_render(&b, y, 128));
		assert(!memcmp(x, y, sizeof(x)));
	}
	session_free(&a);
	session_free(&b);
	score_view_free(&av);
	score_view_free(&bv);
	for (size_t n = 0; n < size; n += 97) {
		assert(score_unpack(&b, &restored, &bv, data, n));
		session_free(&b);
		score_view_free(&bv);
	}
	free(data);
	puts(
	    "OK: worker description transfer, independently owned metadata, identical PCM and truncated transfer rejection");
}

static void test_note_obligations(void) {
	const char *base = "(import ../lib/pattern :as p) "
	                   "(def tone (daw/plugin :tone \"build/test/fixture.perone\" {:gain 0.25})) "
	                   "(daw/output (daw/track tone)) (daw/tempo 120) "
	                   "(daw/score (p/loop (p/events 8 %s)) {:quantum 2})";
	Session a = {.sample_rate = 48000}, b = {.sample_rate = 48000, .describe = 1};
	Output output;
	char code[2048];
	snprintf(code, sizeof(code), base, "[[0 6 [:note tone 60 100]]]");
	assert(!prepare_score(&a, &output, "test/notes.janet", code, NULL, NULL));
	snprintf(code, sizeof(code), base, "[[2.5 3.5 [:note tone 60 100]] [5.8 7 [:note tone 60 100]]]");
	assert(!prepare_score(&b, &output, "test/notes.janet", code, NULL, NULL));
	advance(&a, 12000);
	assert(!session_update(&a, &b, 12000, 1, NULL));
	float audio[2];
	advance(&a, 48000);
	assert(!session_render(&a, audio, 1) && audio[0] == .25f); // Old note continues across the revision.
	advance(&a, 84000);
	assert(!session_render(&a, audio, 1) && audio[0] == 0); // Replacement's own note-off.
	advance(&a, 144000);
	assert(!session_render(&a, audio, 1) && audio[0] == .25f); // Old note-off cannot silence a newer retrigger.
	advance(&a, 168000);
	assert(!session_render(&a, audio, 1) && audio[0] == 0);
	session_free(&a);
	session_free(&b);
	puts("OK: cross-revision note-offs, retriggers and obsolete note-off suppression");
}

static void test_cycle_boundary(void) {
	Session s = {.sample_rate = 48000};
	Output output;
	const char *code = "(import ../lib/pattern :as p) "
	                   "(def tone (daw/plugin :tone \"build/test/fixture.perone\")) (daw/output (daw/track tone)) "
	                   "(daw/score (p/loop (p/map |[:param tone :gain $] (p/curve 2 2 |(+ .2 (* .6 $))))))";
	assert(!prepare_score(&s, &output, "test/boundary.janet", code, NULL, NULL));
	advance(&s, 48000);
	float audio[2];
	assert(!session_render(&s, audio, 1));
	assert(fabsf(audio[0] - .2f) < 1e-6); // End of previous cycle, then start of next cycle.
	session_free(&s);
	puts("OK: previous cycle endpoints precede the next cycle's initial controls");
}

static void test_identity(void) {
	const char *head = "(import ../lib/pattern :as p) ";
	const char *tone = "(def tone (daw/plugin :tone \"build/test/fixture.perone\" {:gain 0.25})) ";
	const char *effect = "(def fx (daw/plugin :fx \"build/test/effect.perone\" {:gain 0.5})) ";
	const char *tail = "(daw/output (daw/track tone {:effects [fx]})) "
	                   "(daw/score (p/loop (p/events 2 [[0 1 [:note tone 60 100]]])) {:quantum 2})";
	Session a = {.sample_rate = 48000}, b = {.sample_rate = 48000, .describe = 1};
	Output output;
	ScoreView view;
	char code[4096];
	snprintf(code, sizeof(code), "%s%s%s%s", head, tone, effect, tail);
	assert(!prepare_score(&a, &output, "test/identity.janet", code, NULL, NULL));
	DSP *synth = a.nodes[0].dsp[0].dsp, *left = a.nodes[1].dsp[0].dsp, *right = a.nodes[1].dsp[1].dsp;
	snprintf(code, sizeof(code), "%s%s%s%s", head, effect, tone, tail);
	char *gain = strstr(code, "0.25");
	assert(gain);
	memcpy(gain, "0.75", 4);
	assert(!prepare_score(&b, &output, "test/identity.janet", code, NULL, &view));
	advance(&a, 1000);
	int mapping[MAX_NODES];
	assert(!session_update(&a, &b, 1000, 1, mapping));
	assert(mapping[0] == 1 && mapping[1] == 0);
	score_view_remap(&view, mapping);
	assert(view.nodes[0].count == 1 && view.nodes[1].count == 0);
	advance(&a, 48000);
	float audio[2];
	assert(!session_render(&a, audio, 1) && audio[0] == .375f && audio[1] == -.375f);
	assert(a.nodes[0].dsp[0].dsp == synth && a.nodes[1].dsp[0].dsp == left && a.nodes[1].dsp[1].dsp == right);
	session_free(&b);
	score_view_free(&view);
	b = (Session){.sample_rate = 48000, .describe = 1};
	char *key = strstr(code, ":tone");
	memcpy(key, ":nope", 5);
	assert(!prepare_score(&b, &output, "test/identity.janet", code, NULL, NULL));
	assert(session_update(&a, &b, 50000, 2, NULL));
	assert(!atomic_load(&a.pending) && a.nodes[0].dsp[0].dsp == synth);
	session_free(&a);
	session_free(&b);
	puts("OK: stable graph identities, declaration reordering, parameter changes, stereo instance reuse and rejection");
}

static int count_origin(const ScoreEvent *event, void *context) {
	(void)event;
	++*(int *)context;
	return 1;
}

static void test_tracking_boundary(void) {
	Sequence sequence = {.at = 48000};
	Session session = {.sample_rate = 48000, .sequence = &sequence};
	ScoreEvent event = {.start = .5, .end = 2, .period = 3, .pitch = 60};
	ScoreView view = {.nnodes = 1, .repeating = 1};
	view.nodes[0].events = &event;
	view.nodes[0].count = 1;
	score_view_activate(&view, &session);
	int count = 0;
	score_view_visit(&view, 0, 1.5, 1.6, 0, count_origin, &count);
	assert(count == 0); // The replacement never started the note whose onset preceded activation.
	score_view_visit(&view, 0, 4, 4.1, 0, count_origin, &count);
	assert(count == 1);
	puts("OK: source tracking excludes hypothetical notes preceding revision activation");
}

static void test_seek(void) {
	const double periods[] = {1.0 / 48000, .1, 1.0 / 7, 60.0 / 137};
	for (size_t p = 0; p < sizeof(periods) / sizeof(*periods); ++p) {
		Sequence *s = calloc(1, sizeof(*s));
		assert(s);
		s->bpm = 120;
		s->quantum = 4;
		assert(!sequence_add(s, (Cue){.start = -.123, .end = -.123, .period = periods[p]}));
		assert(!sequence_prepare(s, 48000, 0));
		uint64_t previous = UINT64_MAX;
		for (int i = 0; i < 10000; ++i) {
			uint64_t time = sequence_next(s);
			sequence_history(s, time);
			assert(sequence_next(s) == previous);
			sequence_seek(s, time);
			assert(sequence_next(s) == time);
			sequence_pop(s);
			assert(sequence_next(s) > time);
			previous = time;
		}
		sequence_free(s);
	}
	puts("OK: seeking at rounded sample boundaries preserves the scheduled occurrence");
}

static void test_transport(void) {
	const char *head = "(import ../lib/pattern :as p) "
	                   "(def tone (daw/plugin :tone \"build/test/fixture.perone\" {:gain 0.25})) "
	                   "(def fx (daw/plugin :fx \"build/test/effect.perone\" {:gain 0.5})) "
	                   "(def track (daw/track tone {:effects [fx]})) (daw/output track) ";
	const char *tails[] = {"(daw/note tone 0 0.6 60) (daw/note tone 0.8 0.2 60) "
	                       "(daw/param tone 0.1 :gain 0.6) (daw/param tone 0.1 :gain 0.8) "
	                       "(daw/param fx 0.2 :gain 0.25) (daw/param track 0.3 :pan -0.5) (daw/end 2)",
	    "(daw/tempo 60) (daw/score (p/loop (p/events 2 ["
	    "[0 0.6 [:note tone 60 100]] [0.8 1 [:note tone 60 100]] "
	    "[0.1 0.1 [:param tone :gain 0.6]] [0.1 0.1 [:param tone :gain 0.8]] "
	    "[0.2 0.2 [:param fx :gain 0.25]] [0.3 0.3 [:param track :pan -0.5]]])))"};
	const unsigned rates[] = {44100, 48000};
	const double positions[] = {0, .01, .1, .1001, .2, .3, .5999, .6, .7, .8, .9, 1, 1.9, .02};
	for (size_t r = 0; r < 2; ++r)
		for (int loop = 0; loop < 2; ++loop) {
			Session a = {.sample_rate = rates[r]}, b = {.sample_rate = rates[r]};
			Output output;
			char code[4096];
			snprintf(code, sizeof(code), "%s%s", head, tails[loop]);
			assert(!prepare_score(&a, &output, "test/transport.janet", code, NULL, NULL));
			assert(!prepare_score(&b, &output, "test/transport.janet", code, NULL, NULL));
			DSP *tone = b.nodes[0].dsp[0].dsp, *left = b.nodes[1].dsp[0].dsp, *right = b.nodes[1].dsp[1].dsp;
			for (size_t j = 0; j < sizeof(positions) / sizeof(*positions); ++j) {
				uint64_t target = llround(positions[j] * rates[r]);
				assert(!session_seek(&a, 0));
				advance(&a, target);
				assert(!session_seek(&b, target));
				float x[128], y[128];
				assert(!session_render(&a, x, 64) && !session_render(&b, y, 64));
				assert(!memcmp(x, y, sizeof(x)));
				assert(
				    b.nodes[0].dsp[0].dsp == tone && b.nodes[1].dsp[0].dsp == left && b.nodes[1].dsp[1].dsp == right);
			}
			uint64_t target = loop ? UINT64_C(1000000) * rates[r] + rates[r] / 2 : rates[r] / 2;
			assert(!session_listen(&b, 0, TRACK_MUTE | TRACK_SOLO));
			assert(!session_seek(&b, target));
			float audio[2];
			assert(!session_render(&b, audio, 1) && audio[0] == 0 && audio[1] == 0);
			assert(session_seek(&b, UINT64_MAX) && b.time == target + 1);
			assert(!session_listen(&b, 0, 0));
			assert(!session_seek(&b, target));
			assert(!session_render(&b, audio, 1) && audio[0] == .2f && audio[1] == -.1f);
			if (loop) {
				assert(b.held[0][60] == UINT64_C(1000000) * rates[r] + (uint64_t)llround(.6 * rates[r]));
				assert(b.next_off == b.held[0][60]);
			} else {
				assert(!session_seek(&b, b.frames));
				assert(!session_render(&b, audio, 0) && session_render(&b, audio, 1));
			}
			session_free(&a);
			session_free(&b);
		}
	puts(
	    "OK: seek restores notes, automation, stereo effects and audition state at 44.1/48 kHz, including distant loops");
}

static void test_seek_retriggers(void) {
	Session s = {.sample_rate = 48000};
	Output output;
	const char *code = "(import ../lib/pattern :as p) "
	                   "(def tone (daw/plugin :tone \"build/test/fixture.perone\")) "
	                   "(daw/output (daw/track tone)) (daw/tempo 60) "
	                   "(daw/score (p/loop (p/events 4 [[0 3 [:note tone 60 90]] [1 1.5 [:note tone 60 80]]])))";
	assert(!prepare_score(&s, &output, "test/retrigger.janet", code, NULL, NULL));
	assert(!session_seek(&s, 60000) && s.held[0][60] == 72000);
	assert(!session_seek(&s, 96000) && !s.held[0][60] && s.next_off == UINT64_MAX);
	float audio[2];
	assert(!session_render(&s, audio, 1) && audio[0] == 0); // A short retrigger ended; the older long note stays off.
	Session next = {.sample_rate = 48000, .describe = 1};
	assert(!prepare_score(&next, &output, "test/retrigger.janet", code, NULL, NULL));
	assert(!session_update(&s, &next, s.time, 1, NULL) && s.pending);
	assert(!session_seek(&s, 48000) && !s.pending);
	session_free(&next);
	session_free(&s);
	puts("OK: seek suppresses obsolete note-offs and cancels a queued revision");
}

static void test_loop_origins(void) {
	const char *head = "(import ../lib/pattern :as p)\n"
	                   "(def tone (daw/plugin :tone \"build/test/fixture.perone\"))\n"
	                   "(daw/output (daw/track tone))\n";
	const char *scores[] = {"(def a (p/loop {:length 2 :events [[0 1 [:note tone 60 100]]]}))\n"
	                        "(def b (p/loop {:length 2 :events [[0 1 [:note tone 60 100]]]}))\n"
	                        "(daw/score (p/parallel [a b]))",
	    "(daw/score {:streams [{:offset 0 :period 2 :events [[0 1 [:note tone 60 100]]]}]})"};
	for (int i = 0; i < 2; ++i) {
		Session s = {.describe = 1};
		ScoreView view;
		Output output;
		char code[2048];
		snprintf(code, sizeof(code), "%s%s", head, scores[i]);
		assert(!prepare_score(&s, &output, "test/loop-origins.janet", code, NULL, &view));
		assert(view.nodes[0].events[0].norigins == (i ? 1 : 2));
		score_view_free(&view);
		session_free(&s);
	}
	puts("OK: equal loops retain both source origins and raw streams receive fallback origins");
}

static void test_projection_boundaries(void) {
	ScoreEvent event = {.start = .1, .end = .3, .period = .1, .pitch = 60};
	ScoreView view = {.nnodes = 1, .repeating = 1};
	view.nodes[0].events = &event;
	view.nodes[0].count = 1;
	for (int i = 0; i < 1000; ++i) {
		double from = event.end + i * event.period, to = event.start + (i + 4) * event.period;
		int expected = 0, found = 0;
		for (int j = 0; j < i + 5; ++j)
			expected += event.start + j * event.period < to && event.end + j * event.period > from;
		assert(score_view_summary(&view, 0, from, to).count == (size_t)expected);
		score_view_visit(&view, 0, from, to, 1, count_origin, &found);
		assert(found == expected);
	}
	assert(!score_view_summary(&view, 0, 1e30, 2e30).count);
	view.end = .2;
	assert(!score_view_summary(&view, 0, .2, .4).count);
	int found = 0;
	score_view_visit(&view, 0, .2, .4, 1, count_origin, &found);
	assert(!found);
	puts("OK: projection boundaries match whole occurrences and stop at the export duration");
}

int main(void) {
	test_snapshot();
	test_note_obligations();
	test_cycle_boundary();
	test_identity();
	test_tracking_boundary();
	test_seek();
	test_transport();
	test_seek_retriggers();
	test_loop_origins();
	test_projection_boundaries();
	Session a = {.sample_rate = 48000}, b = {.sample_rate = 48000};
	ScoreView view;
	prepare(&a, NULL, 1, 0);
	prepare(&b, &view, 1, 0);
	assert(view.repeating && view.nodes[0].count == 4 && view.norigins);
	assert(score_view_summary(&view, 0, 1, 2).count == 2);
	float x[514], y[514];
	while (a.time < a.frames) {
		size_t n = a.frames - a.time < 257 ? a.frames - a.time : 257;
		assert(!session_render(&a, x, n));
		for (size_t i = 0; i < n; ++i)
			assert(!session_render(&b, y + 2 * i, 1));
		assert(!memcmp(x, y, n * 2 * sizeof(float)));
	}
	session_free(&a);
	session_free(&b);
	score_view_free(&view);
	a.sample_rate = 48000;
	prepare(&a, &view, 0, 0);
	assert(a.frames == UINT64_MAX && view.end == 0);
	assert(score_view_summary(&view, 0, 1000000, 1000001).count == 2);
	DSP *instance = a.nodes[0].dsp[0].dsp;
	advance(&a, 30000);
	b = (Session){.sample_rate = 48000, .describe = 1};
	prepare(&b, NULL, 0, 1);
	assert(!b.nodes[0].dsp[0].dsp);
	assert(!session_update(&a, &b, 30000, 2, NULL));
	assert(atomic_load(&a.pending)->at == 96000);
	advance(&a, 96000);
	assert(atomic_load(&a.revision) == 0);
	advance(&a, 96001);
	assert(atomic_load(&a.revision) == 2 && a.nodes[0].dsp[0].dsp == instance);
	assert(!atomic_load(&a.pending));
	session_collect(&a);
	session_free(&b);
	assert(!session_seek(&a, 0));
	assert(a.nodes[0].dsp[0].dsp == instance);
	// The sample clock must survive the wasm32 size_t boundary without wrapping.
	a.time = UINT64_C(1) << 32;
	a.nodes[0].dsp[0].time = a.time;
	sequence_seek(a.sequence, a.time);
	advance(&a, (UINT64_C(1) << 32) + 1000);
	session_free(&a);
	score_view_free(&view);
	puts("OK: finite/infinite sequences, trace, analytic projection, block invariance, atomic revision and DSP reuse");
}
