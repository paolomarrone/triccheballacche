#include "score_view.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int by_time(const void *aa, const void *bb) {
	const Event *a = aa, *b = bb;
	return a->time == b->time ? (a->order > b->order) - (a->order < b->order) : a->time > b->time ? 1 : -1;
}

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

int main(void) {
	// Deliberately schedule out of time order, with overlapping equal pitches and nested durations.
	// No DSP and no provenance are needed to construct or query a projection.
	Session session = {.nnodes = 1, .ntracks = 1, .sample_rate = 48000, .sealed = 1};
	session.nodes[0].events = calloc(700, sizeof(Event));
	assert(session.nodes[0].events);
	ScoreEvent expected[400];
	size_t count = 0, raw = 0;
	for (size_t i = 0; i < 300; ++i) {
		size_t start = ((i * 7919) % 10000), end = start + 1 + ((i * 97) % 6000);
		expected[count++] = (ScoreEvent){
		    .start = start / 48000.0, .end = end / 48000.0, .pitch = 60 + i % 12, .velocity = 80, .order = raw};
		session.nodes[0].events[raw] =
		    (Event){.time = start, .parameter = -1, .midi = {0x90, 60 + i % 12, 80}, .order = raw};
		++raw;
		session.nodes[0].events[raw] =
		    (Event){.time = end, .parameter = -1, .midi = {0x80, 60 + i % 12, 0}, .order = raw};
		++raw;
	}
	for (size_t i = 0; i < 100; ++i) {
		size_t start = i * 99;
		expected[count++] = (ScoreEvent){.start = start / 48000.0, .end = start / 48000.0, .pitch = -1, .order = raw};
		session.nodes[0].events[raw] = (Event){.time = start, .parameter = 0, .order = raw};
		++raw;
	}
	session.nodes[0].count = raw;
	session.frames = 20000;
	qsort(session.nodes[0].events, raw, sizeof(Event), by_time);
	ScoreView view;
	assert(!score_view_init(&view, &session) && !score_view_index(&view));
	free(session.nodes[0].events); // Projection must survive destruction of its source.
	assert(view.nodes[0].count == count && !view.norigins);
	for (size_t i = 0; i < count; ++i) {
		const ScoreEvent *actual = score_view_find(&view, 0, expected[i].order);
		assert(actual && actual->start == expected[i].start && actual->end == expected[i].end);
		assert(actual->pitch == expected[i].pitch);
		if (actual->pitch >= 0)
			assert(!score_view_find(&view, 0, actual->order + 1));
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
	puts("OK: projection ownership, exact note pairing, indexed overlaps/density, short source pulses and large times");
}
