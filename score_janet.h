#ifndef SCORE_JANET_H
#define SCORE_JANET_H
#include "score_view.h"
#include "session.h"

typedef struct {
	int pcm16;
	float normalize;
} Output;

int load_score(Session *session, Output *output, const char *path);
// NULL source reads the file; otherwise evaluate unsaved text at its original path.
// Optional documents is a JSON object of paths to unsaved Janet module sources.
// Score must be empty; free it after a failed preparation as well as after use.
// Optional diagnostics and view are owned by the caller. The view must be empty;
// requesting it enables source annotations during preparation. Failure leaves it empty.
int prepare_score(Score *score, Output *output, const char *path, const char *source, char **diagnostics,
    ScoreView *view, const char *documents);
// Publish the complete WAV on success; preserve the destination on failure.
int write_score(Session *session, const Output *output, const char *path);
#endif
