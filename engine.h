#ifndef ENGINE_H
#define ENGINE_H
#include "loader.h"

enum { SAMPLE_RATE = 44100, BLOCK = 512 };
typedef struct {
	size_t time;
	int parameter; // -1: MIDI, otherwise parameter index
	float value;
	uint8_t midi[3];
	size_t order; // Stable tie breaker for generated scores.
} Event;
typedef struct {
	TibiaModule *module;
	void *instance, *memory;
	const Event *events;
	size_t count, next, time;
} Engine;

void close_engine(Engine *e);
int open_engine(Engine *e, const char *path);
void render(Engine *e, float *out, const float *in, size_t frames);
#endif
