#include "support.h"
#include "snapshot.h"
#include "posix/module.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *source = "(import ../lib/pattern :as p)\n"
                            "(def tone (daw/plugin :tone \"build/test/fixture.perone\" {:gain 0.25}))\n"
                            "(daw/output (daw/track tone))\n"
                            "(daw/tempo 120)\n"
                            "(daw/score (p/parallel [(p/loop (p/map |[:note tone $ 100] (p/steps 1 [60 64])))\n"
                            " (p/loop (p/map |[:param tone :gain $] (p/events 3 [[0 0 0.2] [1 1 0.6]])))]) %s)";

static void prepare(Score *s, ScoreView *view, int finite, int changed) {
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
	Score a = {.sample_rate = 48000}, b = {0};
	ScoreView av, bv = {0};
	Output output = {0}, restored;
	prepare(&a, &av, 0, 0);
	size_t size, other_size;
	void *data = score_pack(&a, &output, &av, &size);
	assert(data && size);
	assert(!score_unpack(&b, &restored, &bv, data, size));
	for (int i = 0; i < av.nnodes; ++i)
		assert(!memcmp(av.nodes[i].defaults, bv.nodes[i].defaults, sizeof(av.nodes[i].defaults)));
	void *other = score_pack(&b, &restored, &bv, &other_size);
	assert(other && other_size == size && !memcmp(data, other, size));
	free(other);
	Session left = {0}, right = {0};
	assert(!session_activate(&left, &a));
	assert(!session_activate(&right, &b));
	float x[256], y[256];
	for (int i = 0; i < 100; ++i) {
		assert(!session_render(&left, x, 128) && !session_render(&right, y, 128));
		assert(!memcmp(x, y, sizeof(x)));
	}
	session_free(&left);
	session_free(&right);
	score_view_free(&av);
	score_view_free(&bv);
	for (size_t n = 0; n < size; n += 97) {
		assert(score_unpack(&b, &restored, &bv, data, n));
		score_free(&b);
		score_view_free(&bv);
	}
	free(data);
	puts(
	    "OK: worker description transfer, independently owned metadata, identical PCM and truncated transfer rejection");
}

static void test_automation(void) {
	const char *code =
	    "(def tone (daw/plugin \"build/test/fixture.perone\" {:gain 0.25})) "
	    "(daw/output (daw/track tone)) (daw/tempo 60) "
	    "(daw/score {:streams ["
	    "{:offset 0 :period 2 :events [[0 0 [:param tone :gain 0.2]] [1 1 [:param tone :gain 0.8]] [2 2 [:param tone :gain 0.4]]]} "
	    "{:offset 0 :period 3 :events [[2 2 [:param tone :gain 0.6]]]} "
	    "{:offset 0 :period 2 :events [[-0.5 -0.5 [:param tone :gain 0.9]]]} "
	    "{:offset 0 :events [[-0.25 -0.25 [:param tone :gain 1]] [0.33333 0.33333 [:param tone :gain 0.3]]]}]})";
	for (unsigned rate = 44100; rate <= 48000; rate += 3900) {
		Session s = {.sample_rate = rate};
		ScoreView view;
		ScoreAutomation out;
		Output output;
		assert(!prepare_session(&s, &output, "test/automation.janet", code, NULL, &view));
		const double positions[] = {0, .33332, .33333, .33334, .5, 1, 1.5, 2, 4, 6, 1000000.33333, 1000000000.5};
		for (size_t i = 0; i < sizeof(positions) / sizeof(*positions); ++i) {
			uint64_t sample = llround(positions[i] * rate);
			double from = (double)sample / rate;
			assert(!score_view_automation(&view, 0, 1, from, from + .01, 32, &out));
			assert(out.count && !out.dense);
			assert(!session_seek(&s, sample));
			float audio[2];
			assert(!session_render(&s, audio, 1) && audio[0] == out.points[0].value);
		}
		assert(!score_view_automation(&view, 0, 1, 0, 1000000, 32, &out));
		assert(out.dense && out.count == 33);
		for (int i = 0; i < 32; ++i) {
			assert(out.low[i] == .2f && out.high[i] == .9f);
			uint64_t sample = (uint64_t)llround(out.points[i + 1].time * rate) - 1;
			assert(!session_seek(&s, sample));
			float audio[2];
			assert(!session_render(&s, audio, 1) && audio[0] == out.points[i + 1].value);
		}
		session_free(&s);
		assert(!score_view_automation(&view, 0, 1, 1000000, 1000001, 32, &out));
		score_view_free(&view);
	}
	puts(
	    "OK: automation matches scheduled PCM at 44.1/48 kHz, including rounded boundaries, pickups, independent loops and distant views");
}

static void test_note_obligations(void) {
	const char *base = "(import ../lib/pattern :as p) "
	                   "(def tone (daw/plugin :tone \"build/test/fixture.perone\" {:gain 0.25})) "
	                   "(daw/output (daw/track tone)) (daw/tempo 120) "
	                   "(daw/score (p/loop (p/events 8 %s)) {:quantum 2})";
	Session a = {.sample_rate = 48000};
	Score b = {.sample_rate = 48000};
	Output output;
	char code[2048];
	snprintf(code, sizeof(code), base, "[[0 6 [:note tone 60 100]]]");
	assert(!prepare_session(&a, &output, "test/notes.janet", code, NULL, NULL));
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
	score_free(&b);
	puts("OK: cross-revision note-offs, retriggers and obsolete note-off suppression");
}

static void test_cycle_boundary(void) {
	Session s = {.sample_rate = 48000};
	Output output;
	const char *code = "(import ../lib/pattern :as p) "
	                   "(def tone (daw/plugin :tone \"build/test/fixture.perone\")) (daw/output (daw/track tone)) "
	                   "(def start (p/events 2 [[0 0 [:param tone :gain .2]]])) "
	                   "(def end (p/events 2 [[2 2 [:param tone :gain .8]]])) "
	                   "(daw/score (p/loop (p/parallel [start end])))";
	assert(!prepare_session(&s, &output, "test/boundary.janet", code, NULL, NULL));
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
	Session a = {.sample_rate = 48000};
	Score b = {.sample_rate = 48000};
	Output output;
	ScoreView view;
	char code[4096];
	snprintf(code, sizeof(code), "%s%s%s%s", head, tone, effect, tail);
	assert(!prepare_session(&a, &output, "test/identity.janet", code, NULL, NULL));
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
	score_free(&b);
	score_view_free(&view);
	b = (Score){.sample_rate = 48000};
	char *key = strstr(code, ":tone");
	memcpy(key, ":nope", 5);
	assert(!prepare_score(&b, &output, "test/identity.janet", code, NULL, NULL));
	assert(session_update(&a, &b, 50000, 2, NULL));
	assert(!atomic_load(&a.pending) && a.nodes[0].dsp[0].dsp == synth);
	session_free(&a);
	score_free(&b);
	puts("OK: stable graph identities, declaration reordering, parameter changes, stereo instance reuse and rejection");
}

static int count_origin(const ScoreEvent *event, void *context) {
	(void)event;
	++*(int *)context;
	return 1;
}

static void test_tracking_boundary(void) {
	ScoreEvent event = {.start = .5, .end = 2, .period = 3, .pitch = 60};
	ScoreView view = {.nnodes = 1, .repeating = 1, .sample_rate = 48000};
	view.nodes[0].events = &event;
	view.nodes[0].count = 1;
	score_view_activate(&view, 48000);
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
		Sequencer cursor = {0};
		assert(!sequencer_init(&cursor, s, 48000, 0));
		uint64_t previous = UINT64_MAX;
		for (int i = 0; i < 10000; ++i) {
			uint64_t time = sequencer_next(&cursor);
			sequencer_history(&cursor, time);
			assert(sequencer_next(&cursor) == previous);
			sequencer_seek(&cursor, time);
			assert(sequencer_next(&cursor) == time);
			sequencer_pop(&cursor);
			assert(sequencer_next(&cursor) > time);
			previous = time;
		}
		sequencer_free(&cursor);
		sequence_free(s);
		free(s);
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
			assert(!prepare_session(&a, &output, "test/transport.janet", code, NULL, NULL));
			assert(!prepare_session(&b, &output, "test/transport.janet", code, NULL, NULL));
			assert(session_frame(&b, .49 / rates[r]) == 0 && session_frame(&b, .51 / rates[r]) == 1);
			assert(session_frame(&b, -1) == UINT64_MAX && session_frame(&b, INFINITY) == UINT64_MAX);
			assert(session_frame(&b, NAN) == UINT64_MAX);
			if (!loop) {
				assert(session_frame(&b, 2 + .4 / rates[r]) == b.frames);
				assert(session_frame(&b, 2 + .6 / rates[r]) == UINT64_MAX);
			}
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
	assert(!prepare_session(&s, &output, "test/retrigger.janet", code, NULL, NULL));
	assert(!session_seek(&s, 60000) && s.held[0][60] == 72000);
	assert(!session_seek(&s, 96000) && !s.held[0][60] && s.next_off == UINT64_MAX);
	float audio[2];
	assert(!session_render(&s, audio, 1) && audio[0] == 0); // A short retrigger ended; the older long note stays off.
	Score next = {.sample_rate = 48000};
	assert(!prepare_score(&next, &output, "test/retrigger.janet", code, NULL, NULL));
	assert(!session_update(&s, &next, s.time, 1, NULL) && s.pending);
	assert(!session_seek(&s, 48000) && !s.pending);
	score_free(&next);
	session_free(&s);
	puts("OK: seek suppresses obsolete note-offs and cancels a queued revision");
}

static void test_loop_origins(void) {
	const char *head = "(import ../lib/pattern :as p)\n"
	                   "(def tone (daw/plugin :tone \"build/test/fixture.perone\"))\n"
	                   "(daw/output (daw/track tone))\n";
	const char *scores[] = {"(def a (p/loop (p/events 2 [[0 1 [:note tone 60 100]]])))\n"
	                        "(def b (p/loop (p/events 2 [[0 1 [:note tone 60 100]]])))\n"
	                        "(daw/score (p/parallel [a b]))",
	    "(daw/score {:streams [{:offset 0 :period 2 :events [[0 1 [:note tone 60 100]]]}]})"};
	for (int i = 0; i < 2; ++i) {
		Score s = {0};
		ScoreView view;
		Output output;
		char code[2048];
		snprintf(code, sizeof(code), "%s%s", head, scores[i]);
		assert(!prepare_score(&s, &output, "test/loop-origins.janet", code, NULL, &view));
		assert(view.nodes[0].events[0].norigins == (i ? 1 : 2));
		score_view_free(&view);
		score_free(&s);
	}
	puts("OK: equal loops retain both source origins and raw streams receive fallback origins");
}

static void test_projection_boundaries(void) {
	ScoreEvent event = {.start = .1, .end = .3, .period = .1, .pitch = 60};
	ScoreView view = {.nnodes = 1, .repeating = 1, .sample_rate = 48000};
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

static void test_prepared_ownership(void) {
	Score score = {.sample_rate = 48000};
	Session session = {0};
	ScoreView view;
	Output output;
	const char *code = "(def tone (daw/plugin \"build/test/fixture.perone\")) "
	                   "(daw/output (daw/track tone)) (daw/note tone 0 1 60) (daw/end 2)";
	assert(!setenv("PERONE_TEST_FAIL", "alloc", 1));
	assert(!prepare_score(&score, &output, "test/ownership.janet", code, NULL, &view));
	Cue *cues = score.sequence.cues;
	char *product = score.nodes[0].info.product;
	assert(product && score.nodes[0].info.maximum[1] == 1);
	assert(session_activate(&session, &score) && !session.active && !session.audio);
	assert(score.sealed && score.sequence.cues == cues && score.nodes[0].info.product == product);
	assert(!unsetenv("PERONE_TEST_FAIL"));
	assert(!session_activate(&session, &score));
	assert(!score.nnodes && !score.sequence.cues);
	assert(session_score(&session)->sequence.cues == cues);
	session_free(&session);
	assert(view.nodes[0].product && score_view_summary(&view, 0, 0, 1).count == 1);
	score_view_free(&view);
	score_free(&score);
	puts("OK: preparation without DSPs, failed activation retains ownership, successful activation moves data");
}

static void test_finite_equivalence(void) {
	const char *head = "(def tone (daw/plugin \"build/test/fixture.perone\" {:gain 0.25})) "
	                   "(daw/output (daw/track tone)) ";
	const char *tails[] = {"(daw/note tone 0 2 60) (daw/note tone 1 2 60) (daw/param tone 1 :gain 0.75) (daw/end 4)",
	    "(daw/tempo 60) (daw/score {:length 4 :streams [{:offset 0 :events [[0 2 [:note tone 60 100]] "
	    "[1 3 [:note tone 60 100]] [1 1 [:param tone :gain 0.75]]]}]})",
	    "(daw/note tone 0 2 60) (daw/tempo 60) (daw/score {:length 4 :streams [{:offset 0 :events "
	    "[[1 3 [:note tone 60 100]] [1 1 [:param tone :gain 0.75]]]}]})"};
	for (unsigned rate = 44100; rate <= 48000; rate += 3900) {
		float *reference = calloc(8 * rate, sizeof(float));
		assert(reference);
		for (int mode = 0; mode < 3; ++mode) {
			Session session = {.sample_rate = rate};
			ScoreView view;
			Output output;
			char code[1024];
			snprintf(code, sizeof(code), "%s%s", head, tails[mode]);
			assert(!prepare_session(&session, &output, "test/finite.janet", code, NULL, &view));
			assert(view.nodes[0].count == 3);
			for (int i = 0; i < 3; ++i)
				assert(view.nodes[0].events[i].norigins);
			float audio[514];
			while (session.time < session.frames) {
				size_t at = session.time, n = session.frames - at;
				size_t block = mode == 1 ? 1 : 257;
				if (n > block)
					n = block;
				assert(!session_render(&session, audio, n));
				if (!mode)
					memcpy(reference + 2 * at, audio, 2 * n * sizeof(float));
				else
					assert(!memcmp(reference + 2 * at, audio, 2 * n * sizeof(float)));
			}
			const uint64_t positions[] = {rate, 2 * rate, 5 * rate / 2, 3 * rate};
			for (size_t i = 0; i < 4; ++i) {
				assert(!session_seek(&session, positions[i]) && !session_render(&session, audio, 1));
				assert(audio[0] == (i < 3 ? .75f : 0));
			}
			session_free(&session);
			score_view_free(&view);
		}
		free(reference);
	}
	// Absolute rounding must accept a one-sample note even when subtraction loses precision.
	Score score = {.sample_rate = 48000};
	PluginConfig config = {.output = 1, .midi = 0};
	int id = score_plugin(&score, "unused", &config);
	assert(id >= 0 && score_output(&score, id) >= 0);
	assert(!score_note(&score, id, 48001, 48002, 60, 100) && !score_end(&score, 48003));
	score_free(&score);
	// A sub-sample repeating note could round to zero length in a later cycle.
	Sequence sequence = {.bpm = 120, .quantum = 4};
	assert(!sequence_add(
	    &sequence, (Cue){.start = .49 / 48000, .end = .51 / 48000, .period = 1.25 / 48000, .parameter = -1}));
	assert(!sequence_valid(&sequence, 48000));
	sequence_free(&sequence);
	puts("OK: finite, pattern and mixed APIs share retrigger, automation, seek, trace and sample rounding semantics");
}

static void test_finite_projection(void) {
	Score score = {.sample_rate = 48000};
	ScoreView view;
	Output output;
	const char *code =
	    "(def tone (daw/plugin \"build/test/fixture.perone\")) "
	    "(daw/output (daw/track tone)) (daw/tempo 60) "
	    "(daw/score {:length 4 :streams [{:offset 0 :events [[-1 1 [:note tone 60 100]] [0 2 [:note tone 60 100]] "
	    "[3 4 [:note tone 60 100]]]}]} {:duration 1.5})";
	assert(!prepare_score(&score, &output, "test/cropped.janet", code, NULL, &view));
	assert(!view.repeating && score_view_summary(&view, 0, 0, 1.5).count == 1);
	assert(!score_view_summary(&view, 0, 1.5, 4).count);
	int found = 0;
	score_view_visit(&view, 0, 0, 1.5, 1, count_origin, &found);
	assert(found == 1);
	found = 0;
	score_view_visit(&view, 0, 1.5, 4, 1, count_origin, &found);
	assert(!found);
	score_view_activate(&view, 24000);
	score_view_visit(&view, 0, 1, 1.1, 0, count_origin, &found);
	assert(!found);
	score_view_free(&view);
	score_free(&score);
	puts("OK: finite projection omits negative pickups, honors export crops and tracks revision activation");
}

typedef struct {
	Session *session;
	atomic_int stop;
	atomic_uint_fast64_t position;
} AudioThread;

static void *render_thread(void *context) {
	AudioThread *audio = context;
	float buffer[34];
	while (!atomic_load(&audio->stop)) {
		assert(!session_render(audio->session, buffer, 17));
		atomic_store(&audio->position, audio->session->time);
	}
	return NULL;
}

static void test_concurrent_revisions(void) {
	const char *code =
	    "(def tone (daw/plugin :tone \"build/test/fixture.perone\")) "
	    "(daw/output (daw/track tone)) (daw/tempo 60) "
	    "(daw/score {:streams [{:offset 0 :period 1 :events [[0 0.5 [:note tone 60 100]]]}]} {:quantum 0.01})";
	Session session = {.sample_rate = 48000};
	Output output;
	assert(!prepare_session(&session, &output, "test/concurrent.janet", code, NULL, NULL));
	DSP *dsp = session.nodes[0].dsp[0].dsp;
	AudioThread audio = {.session = &session};
	pthread_t thread;
	assert(!pthread_create(&thread, NULL, render_thread, &audio));
	for (unsigned i = 1; i <= 100; ++i) {
		Score next = {.sample_rate = 48000};
		assert(!prepare_score(&next, &output, "test/concurrent.janet", code, NULL, NULL));
		assert(!session_update(&session, &next, atomic_load(&audio.position) + 4096, i, NULL));
		assert(!next.nnodes && !next.sequence.cues);
		while (atomic_load(&session.revision) != i || atomic_load(&session.pending))
			sched_yield();
		session_collect(&session);
		const Score *active = session_score(&session);
		assert(active->sequence.count == 1 && active->nodes[0].info.product);
		assert(session.nodes[0].dsp[0].dsp == dsp);
	}
	atomic_store(&audio.stop, 1);
	assert(!pthread_join(thread, NULL));
	session_free(&session);
	puts("OK: concurrent revision publication, metadata ownership, cursor replacement and retirement");
}

int main(void) {
	test_finite_projection();
	test_concurrent_revisions();
	test_prepared_ownership();
	test_finite_equivalence();
	test_automation();
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
	Score description = {.sample_rate = 48000};
	prepare(&description, NULL, 1, 0);
	assert(!session_activate(&a, &description));
	description.sample_rate = 48000;
	prepare(&description, &view, 1, 0);
	assert(!session_activate(&b, &description));
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
	description.sample_rate = 48000;
	prepare(&description, &view, 0, 0);
	assert(!session_activate(&a, &description));
	assert(a.frames == UINT64_MAX && view.end == 0);
	assert(score_view_summary(&view, 0, 1000000, 1000001).count == 2);
	DSP *instance = a.nodes[0].dsp[0].dsp;
	advance(&a, 30000);
	description.sample_rate = 48000;
	prepare(&description, NULL, 0, 1);
	assert(!session_update(&a, &description, 30000, 2, NULL));
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
	assert(!session_seek(&a, UINT64_C(1) << 32));
	advance(&a, (UINT64_C(1) << 32) + 1000);
	session_free(&a);
	score_view_free(&view);
	puts("OK: finite/infinite sequences, trace, analytic projection, block invariance, atomic revision and DSP reuse");
}
