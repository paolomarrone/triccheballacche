#ifndef SNAPSHOT_H
#define SNAPSHOT_H
#include "score_janet.h"

// Private, same-build transfer between preparation and playback workers. Not a file format.
// The receiver owns independent allocations; plugin instances and pointers never cross workers.
void *score_pack(const Score *s, const Output *output, const ScoreView *view, size_t *length);
int score_unpack(Score *s, Output *output, ScoreView *view, const void *data, size_t length);
#endif
