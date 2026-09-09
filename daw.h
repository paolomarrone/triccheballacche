#ifndef DAW_H
#define DAW_H
#include "session.h"

typedef struct {
	int pcm16;
	float normalize;
} Output;

int load_score(Session *session, Output *output, const char *path);
int write_score(Session *session, const Output *output, const char *path);
#endif
