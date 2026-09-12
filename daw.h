#ifndef DAW_H
#define DAW_H
#include "session.h"

typedef struct {
	int pcm16;
	float normalize;
} Output;

int load_score(Session *session, Output *output, const char *path);
// NULL source reads the file; otherwise evaluate unsaved text at its original path.
// Optional diagnostics are allocated for the caller to free. Setup is synchronous.
int prepare_score(Session *session, Output *output, const char *path, const char *source, char **diagnostics);
// Publish the complete WAV on success; preserve the destination on failure.
int write_score(Session *session, const Output *output, const char *path);
#endif
