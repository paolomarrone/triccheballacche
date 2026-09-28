#ifndef SEQUENCE_H
#define SEQUENCE_H
#include <stddef.h>
#include <stdint.h>

enum { MAX_CUES = 1048576 };

// One finite event template. A positive period repeats from occurrence zero.
// Times are seconds; conversion to samples always uses the absolute occurrence.
typedef struct {
	double start, end, period;
	int node, parameter, pitch, velocity, stream;
	float value;
} Cue;

typedef struct {
	uint64_t time, cycle;
	size_t cue;
} SequenceCursor;

typedef struct {
	Cue *cues;
	size_t count, capacity;
	double bpm, quantum;
} Sequence;

typedef struct {
	const Sequence *sequence;
	SequenceCursor *heap;
	size_t used;
	unsigned rate;
} Sequencer;

int sequence_add(Sequence *s, Cue cue);
int sequence_valid(const Sequence *s, unsigned rate);
void sequence_free(Sequence *s);
// Prepare all cursor storage on the control thread; playback never allocates.
int sequencer_init(Sequencer *s, const Sequence *sequence, unsigned rate, uint64_t from);
void sequencer_seek(Sequencer *s, uint64_t from);
// Last occurrence of each template strictly before from, in playback order.
// Use shift (no repetition), then seek to resume forward iteration.
void sequencer_history(Sequencer *s, uint64_t from);
void sequencer_shift(Sequencer *s);
uint64_t sequencer_next(const Sequencer *s);
uint64_t sequence_boundary(const Sequence *s, unsigned rate, uint64_t after);
// Parameters precede notes at a shared sample, followed by source insertion order.
const Cue *sequencer_peek(const Sequencer *s, uint64_t *end);
void sequencer_pop(Sequencer *s);
void sequencer_free(Sequencer *s);
#endif
