#ifndef SNAPSHOT_H
#define SNAPSHOT_H
#include "daw.h"

// Private, same-build transfer between preparation and playback workers. Not a file format.
// The receiver owns independent allocations; plugin instances and pointers never cross workers.
void *score_pack(const Session *s, const Output *output, const ScoreView *view, size_t *length);
int score_unpack(Session *s, Output *output, ScoreView *view, const void *data, size_t length);
#endif
