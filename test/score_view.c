#include "score_view.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	const ScoreEvent *events[400];
	size_t count;
} Found;

static int collect(const ScoreEvent *event, void *context) {
	Found *found = context;
	assert(found->count < 400);
	found->events[found->count++] = event;
	return 1;
}

static void automation(void) {
	Score s = {.nnodes = 1, .sample_rate = 48000, .frames = 480000};
	Cue events[] = {{.start = 2, .end = 2, .parameter = 0, .value = 2},
	    {.start = 1, .end = 1, .parameter = 0, .value = 1}, {.start = 1, .end = 1, .parameter = 0, .value = 1.5f},
	    {.start = .5, .end = .5, .parameter = 1, .value = -.5f}};
	s.sequence = (Sequence){.cues = events, .count = 4};
	s.nodes[0].config = (PluginConfig){.nparams = 2, .defaults = {.25f, 0}};
	ScoreView view;
	ScoreAutomation out;
	assert(!score_view_init(&view, &s) && !score_view_index(&view));
	assert(!score_view_automation(&view, 0, 0, 0, 4, 16, &out));
	assert(!out.dense && out.count == 3 && out.points[0].value == .25f && out.points[0].order == UINT64_MAX);
	assert(out.points[1].time == 1 && out.points[1].value == 1.5f && out.points[1].order == 2);
	assert(out.points[2].time == 2 && out.points[2].value == 2);
	assert(!score_view_automation(&view, 0, 0, 1, 2, 16, &out));
	assert(out.count == 1 && out.points[0].value == 1.5f && out.points[0].order == 2);
	assert(!score_view_automation(&view, 0, 0, 1.25, 1.75, 16, &out));
	assert(out.count == 1 && out.points[0].time == 1.25 && out.points[0].value == 1.5f);
	assert(!score_view_automation(&view, 0, 1, 0, 1, 16, &out));
	assert(out.count == 2 && out.points[0].value == 0 && out.points[1].value == -.5f);
	assert(!score_view_automation(&view, 0, 0, 11, 12, 16, &out) && !out.count);
	assert(score_view_automation(&view, 0, 2, 0, 4, 16, &out));
	assert(score_view_automation(&view, -1, 0, 0, 4, 16, &out));
	assert(score_view_automation(&view, 0, 0, NAN, 4, 16, &out));
	assert(score_view_automation(&view, 0, 0, 0, 4, 513, &out));
	score_view_free(&view);

	// Dense finite changes preserve a brief spike as well as the held value in empty bins.
	s.sequence.count = 1200;
	s.sequence.cues = calloc(1200, sizeof(Cue));
	assert(s.sequence.cues);
	for (int i = 0; i < 1200; ++i)
		s.sequence.cues[i] = (Cue){.start = i / 1000.0, .end = i / 1000.0, .parameter = 0, .value = i == 591 ? 3 : .5f};
	assert(!score_view_init(&view, &s) && !score_view_index(&view));
	free(s.sequence.cues);
	assert(!score_view_automation(&view, 0, 0, 0, 4, 16, &out));
	assert(out.dense && out.count == 17);
	for (int i = 0; i < 16; ++i) {
		assert(out.low[i] == .5f && out.high[i] == (i == 2 ? 3 : .5f));
		assert(out.points[i + 1].value == .5f);
	}
	score_view_free(&view);
	puts(
	    "OK: automation defaults, simultaneous writes, held values, parameter isolation and bounded peak-preserving bins");
}

int main(void) {
	automation();
	// Deliberately schedule out of time order, with overlapping equal pitches and nested durations.
	// No DSP and no provenance are needed to construct or query a projection.
	Score session = {.nnodes = 1, .ntracks = 1, .sample_rate = 48000, .sealed = 1};
	session.sequence.cues = calloc(400, sizeof(Cue));
	assert(session.sequence.cues);
	ScoreEvent expected[400];
	size_t count = 0, raw = 0;
	for (size_t i = 0; i < 300; ++i) {
		size_t start = ((i * 7919) % 10000), end = start + 1 + ((i * 97) % 6000);
		expected[count++] = (ScoreEvent){
		    .start = start / 48000.0, .end = end / 48000.0, .pitch = 60 + i % 12, .velocity = 80, .order = raw};
		session.sequence.cues[raw++] = (Cue){
		    .start = start / 48000.0, .end = end / 48000.0, .parameter = -1, .pitch = 60 + i % 12, .velocity = 80};
	}
	for (size_t i = 0; i < 100; ++i) {
		size_t start = i * 99;
		expected[count++] = (ScoreEvent){.start = start / 48000.0, .end = start / 48000.0, .pitch = -1, .order = raw};
		session.sequence.cues[raw] = (Cue){.start = start / 48000.0, .end = start / 48000.0, .parameter = 0};
		++raw;
	}
	session.sequence.count = raw;
	session.frames = 20000;
	ScoreView view;
	assert(!score_view_init(&view, &session) && !score_view_index(&view));
	free(session.sequence.cues); // Projection must survive destruction of its source.
	assert(view.nodes[0].count == count && !view.norigins);
	for (size_t i = 0; i < count; ++i) {
		const ScoreEvent *actual = score_view_find(&view, 0, expected[i].order);
		assert(actual && actual->start == expected[i].start && actual->end == expected[i].end);
		assert(actual->pitch == expected[i].pitch);
	}
	for (int i = 0; i < 1000; ++i) {
		double from = ((i * 313) % 20000) / 48000.0, to = from + (i % 51 + 1) / 1000.0;
		for (int notes = 0; notes <= 1; ++notes) {
			Found found = {0};
			score_view_visit(&view, 0, from, to, notes, collect, &found);
			size_t n = 0;
			int low = 128, high = -1;
			for (size_t j = 0; j < count; ++j) {
				const ScoreEvent *e = expected + j;
				double end = notes ? e->end : fmax(e->end, e->start + 0.08);
				if (e->start >= to || end <= from || (notes && e->pitch < 0))
					continue;
				++n;
				int present = 0;
				for (size_t k = 0; k < found.count; ++k)
					present += found.events[k]->order == e->order;
				assert(present == 1);
				if (e->pitch < low)
					low = e->pitch;
				if (e->pitch > high)
					high = e->pitch;
			}
			assert(found.count == n);
			if (notes) {
				ScoreSummary summary = score_view_summary(&view, 0, from, to);
				assert(summary.count == n);
				assert(!n || (summary.low == low && summary.high == high));
			}
		}
	}
	// Queries and indexes use absolute seconds without a song-length or canvas-size assumption.
	view.end = 0;
	for (size_t i = 0; i < view.nodes[0].count; ++i) {
		view.nodes[0].events[i].start += 1e9;
		view.nodes[0].events[i].end += 1e9;
	}
	free(view.nodes[0].by_order);
	view.nodes[0].by_order = NULL;
	assert(!score_view_index(&view));
	assert(score_view_summary(&view, 0, 1e9, 1e9 + 1).count == 300);
	assert(!score_view_summary(&view, 0, 0, 1).count);
	assert(!score_view_summary(&view, -1, 0, 1).count);
	assert(!score_view_summary(&view, 0, NAN, 1).count);
	score_view_free(&view);
	assert(!view.nnodes && !view.nreferences);
	puts("OK: projection ownership, note durations, indexed overlaps/density, short source pulses and large times");
}
