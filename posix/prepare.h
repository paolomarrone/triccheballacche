#ifndef POSIX_PREPARE_H
#define POSIX_PREPARE_H
#include "score_janet.h"
// Evaluate on a separate thread. Interrupt CPU-bound Janet evaluation after five seconds.
int prepare_background(Score *score, Output *output, const char *path, const char *source, char **diagnostics,
    ScoreView *view, const char *documents);
#endif
