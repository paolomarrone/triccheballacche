#ifndef POSIX_PREPARE_H
#define POSIX_PREPARE_H
#include "daw.h"
// Evaluate on a separate thread. Interrupt CPU-bound Janet evaluation after two seconds.
int prepare_background(
    Session *session, Output *output, const char *path, const char *source, char **diagnostics, ScoreView *view);
#endif
