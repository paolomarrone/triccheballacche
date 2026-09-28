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
	if (s->count >= MAX_CUES || !isfinite(cue.start) || !isfinite(cue.end) || !isfinite(cue.period) ||
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

static int before(const Sequencer *s, SequenceCursor a, SequenceCursor b) {
	if (a.time != b.time)
		return a.time < b.time;
	int pa = s->sequence->cues[a.cue].parameter < 0, pb = s->sequence->cues[b.cue].parameter < 0;
	if (pa != pb)
		return pa < pb;
	int sa = s->sequence->cues[a.cue].stream, sb = s->sequence->cues[b.cue].stream;
	if (sa != sb)
		return sa < sb;
	return a.cycle != b.cycle ? a.cycle < b.cycle : a.cue < b.cue;
}

static void down(Sequencer *s, size_t i) {
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

static void seek(Sequencer *s, uint64_t from, int history) {
	s->used = 0;
	for (size_t i = 0; i < s->sequence->count; ++i) {
		const Cue *c = s->sequence->cues + i;
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

void sequencer_seek(Sequencer *s, uint64_t from) {
	seek(s, from, 0);
}

void sequencer_history(Sequencer *s, uint64_t from) {
	seek(s, from, 1);
}

int sequence_valid(const Sequence *s, unsigned rate) {
	if (!rate || rate > 384000 || !isfinite(s->bpm) || s->bpm <= 0 || !isfinite(s->quantum) || s->quantum <= 0 ||
	    s->quantum * 60 / s->bpm * rate < 1 || s->count > MAX_CUES)
		return 0;
	for (size_t i = 0; i < s->count; ++i) {
		const Cue *c = s->cues + i;
		if (!isfinite(c->start) || !isfinite(c->end) || !isfinite(c->period) || c->period < 0 || c->end < c->start ||
		    fabs(c->start * rate) >= 0x1p52 || fabs(c->end * rate) >= 0x1p52 || (c->period && c->period * rate < 1) ||
		    (c->parameter < 0 &&
		        (llround(c->end * rate) <= llround(c->start * rate) || (c->period && (c->end - c->start) * rate < 1))))
			return 0;
	}
	return 1;
}

int sequencer_init(Sequencer *s, const Sequence *sequence, unsigned rate, uint64_t from) {
	if (s->heap || !sequence_valid(sequence, rate))
		return -1;
	s->heap = calloc(sequence->count ? sequence->count : 1, sizeof(SequenceCursor));
	if (!s->heap)
		return -1;
	s->sequence = sequence;
	s->rate = rate;
	sequencer_seek(s, from);
	return 0;
}

uint64_t sequence_boundary(const Sequence *s, unsigned rate, uint64_t after) {
	double grid = s->quantum * 60 / s->bpm * rate;
	double beat = floor(((double)after + .5) / grid) + 1;
	double value = beat * grid;
	return value < 0x1p53 ? (uint64_t)llround(value) : UINT64_MAX;
}

uint64_t sequencer_next(const Sequencer *s) {
	return s && s->used ? s->heap[0].time : UINT64_MAX;
}

const Cue *sequencer_peek(const Sequencer *s, uint64_t *end) {
	if (!s->used)
		return NULL;
	const SequenceCursor *cursor = s->heap;
	const Cue *cue = s->sequence->cues + cursor->cue;
	if (end)
		*end = sample(cue->end + cursor->cycle * cue->period, s->rate);
	return cue;
}

void sequencer_pop(Sequencer *s) {
	SequenceCursor *cursor = s->heap;
	const Cue *cue = s->sequence->cues + cursor->cue;
	uint64_t time = cue->period ? sample(cue->start + (cursor->cycle + 1) * cue->period, s->rate) : UINT64_MAX;
	if (time != UINT64_MAX && time > cursor->time) {
		cursor->time = time;
		++cursor->cycle;
	} else {
		sequencer_shift(s);
		return;
	}
	if (s->used)
		down(s, 0);
}

void sequencer_shift(Sequencer *s) {
	s->heap[0] = s->heap[--s->used];
	if (s->used)
		down(s, 0);
}

void sequence_free(Sequence *s) {
	free(s->cues);
	*s = (Sequence){0};
}

void sequencer_free(Sequencer *s) {
	free(s->heap);
	*s = (Sequencer){0};
}
