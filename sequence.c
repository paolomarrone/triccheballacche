#include "sequence.h"
#include <math.h>
#include <stdlib.h>

// Doubles represent every integer sample in this range, including on wasm32.
static uint64_t sample(double seconds, unsigned rate) {
	double value = seconds * rate;
	if (!isfinite(value) || value >= 0x1p53)
		return UINT64_MAX;
	return value < 0 ? 0 : (uint64_t)llround(value);
}

int sequence_add(Sequence *s, Cue cue) {
	if (s->heap || s->count == 65536 || !isfinite(cue.start) || !isfinite(cue.end) || !isfinite(cue.period) ||
	    cue.end < cue.start || cue.period < 0)
		return -1;
	if (s->count == s->capacity) {
		size_t capacity = s->capacity ? s->capacity * 2 : 64;
		Cue *cues = realloc(s->cues, capacity * sizeof(*cues));
		if (!cues)
			return -1;
		s->cues = cues;
		s->capacity = capacity;
	}
	s->cues[s->count++] = cue;
	return 0;
}

static int before(const Sequence *s, SequenceCursor a, SequenceCursor b) {
	if (a.time != b.time)
		return a.time < b.time;
	int pa = s->cues[a.cue].parameter < 0, pb = s->cues[b.cue].parameter < 0;
	if (pa != pb)
		return pa < pb;
	int sa = s->cues[a.cue].stream, sb = s->cues[b.cue].stream;
	if (sa != sb)
		return sa < sb;
	return a.cycle != b.cycle ? a.cycle < b.cycle : a.cue < b.cue;
}

static void down(Sequence *s, size_t i) {
	SequenceCursor item = s->heap[i];
	while (2 * i + 1 < s->used) {
		size_t child = 2 * i + 1;
		if (child + 1 < s->used && before(s, s->heap[child + 1], s->heap[child]))
			++child;
		if (!before(s, s->heap[child], item))
			break;
		s->heap[i] = s->heap[child];
		i = child;
	}
	s->heap[i] = item;
}

static void seek(Sequence *s, uint64_t from, int history) {
	s->used = 0;
	for (size_t i = 0; i < s->count; ++i) {
		const Cue *c = s->cues + i;
		double t = (double)from / s->rate;
		uint64_t cycle = c->period && t > c->start ? (uint64_t)floor((t - c->start) / c->period) : 0;
		// Check the preceding candidate too: rounding can place it exactly at from.
		if (cycle)
			--cycle;
		uint64_t time = sample(c->start + cycle * c->period, s->rate);
		// Negative pickups are omitted at the beginning; they are not clamped into a note-on at zero.
		while (time < from || c->start + cycle * c->period < 0) {
			if (!c->period) {
				time = UINT64_MAX;
				break;
			}
			time = sample(c->start + ++cycle * c->period, s->rate);
		}
		if (history) {
			if (c->period && !cycle)
				continue;
			if (c->period)
				--cycle;
			double start = c->start + cycle * c->period;
			time = sample(start, s->rate);
			if (start < 0 || time >= from)
				continue;
		}
		if (time != UINT64_MAX)
			s->heap[s->used++] = (SequenceCursor){time, cycle, i};
	}
	for (size_t i = s->used / 2; i-- > 0;)
		down(s, i);
}

void sequence_seek(Sequence *s, uint64_t from) {
	seek(s, from, 0);
}

void sequence_history(Sequence *s, uint64_t from) {
	seek(s, from, 1);
}

int sequence_prepare(Sequence *s, unsigned rate, uint64_t from) {
	if (!rate || s->heap || !isfinite(s->bpm) || s->bpm <= 0 || !isfinite(s->quantum) || s->quantum <= 0 ||
	    s->quantum * 60 / s->bpm * rate < 1)
		return -1;
	s->rate = rate;
	for (size_t i = 0; i < s->count; ++i) {
		Cue *c = s->cues + i;
		if ((c->period && c->period * rate < 1) || (c->parameter < 0 && (c->end - c->start) * rate < 1) ||
		    fabs(c->start * rate) >= 0x1p52 || fabs(c->end * rate) >= 0x1p52)
			return -1;
	}
	s->heap = calloc(s->count ? s->count : 1, sizeof(SequenceCursor));
	if (!s->heap)
		return -1;
	sequence_seek(s, from);
	return 0;
}

uint64_t sequence_boundary(const Sequence *s, uint64_t after) {
	double grid = s->quantum * 60 / s->bpm * s->rate;
	double beat = floor(((double)after + .5) / grid) + 1;
	double value = beat * grid;
	return value < 0x1p53 ? (uint64_t)llround(value) : UINT64_MAX;
}

uint64_t sequence_next(const Sequence *s) {
	return s && s->used ? s->heap[0].time : UINT64_MAX;
}

const Cue *sequence_peek(const Sequence *s, uint64_t *end) {
	if (!s->used)
		return NULL;
	const SequenceCursor *cursor = s->heap;
	const Cue *cue = s->cues + cursor->cue;
	if (end)
		*end = sample(cue->end + cursor->cycle * cue->period, s->rate);
	return cue;
}

void sequence_pop(Sequence *s) {
	SequenceCursor *cursor = s->heap;
	const Cue *cue = s->cues + cursor->cue;
	uint64_t time = cue->period ? sample(cue->start + (cursor->cycle + 1) * cue->period, s->rate) : UINT64_MAX;
	if (time != UINT64_MAX && time > cursor->time) {
		cursor->time = time;
		++cursor->cycle;
	} else {
		sequence_shift(s);
		return;
	}
	if (s->used)
		down(s, 0);
}

void sequence_shift(Sequence *s) {
	s->heap[0] = s->heap[--s->used];
	if (s->used)
		down(s, 0);
}

void sequence_free(Sequence *s) {
	if (!s)
		return;
	free(s->cues);
	free(s->heap);
	free(s->defaults);
	free(s);
}
