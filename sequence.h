#ifndef SEQUENCE_H
#define SEQUENCE_H
#include <stddef.h>
#include <stdint.h>

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

typedef struct Sequence {
	Cue *cues;
	SequenceCursor *heap;
	size_t count, capacity, used;
	double bpm, quantum;
	unsigned rate, revision;
	uint64_t at;
	float *defaults; // Declared node parameters, separate from the audio-owned current values.
} Sequence;

int sequence_add(Sequence *s, Cue cue);
int sequence_prepare(Sequence *s, unsigned rate, uint64_t from);
void sequence_seek(Sequence *s, uint64_t from);
uint64_t sequence_next(const Sequence *s);
uint64_t sequence_boundary(const Sequence *s, uint64_t after);
// Peek/pop preserve parameter-before-note and source insertion order at the same sample.
const Cue *sequence_peek(const Sequence *s, uint64_t *end);
void sequence_pop(Sequence *s);
void sequence_free(Sequence *s);
#endif
