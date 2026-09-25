#ifndef ENGINE_H
#define ENGINE_H
#include "loader.h"

enum { DEFAULT_SAMPLE_RATE = 44100, BLOCK = 512 };

typedef struct {
	uint64_t time;
	int parameter; // -1: MIDI, otherwise parameter index
	float value;
	uint8_t midi[3];
	size_t order; // Stable tie breaker for generated scores.
} Event;

typedef struct {
	DSP *dsp;
	Modules *modules; // Optional borrowed cache; NULL gives this engine a private module reference.
	PluginConfig config;
	const Event *events;
	size_t count, next;
	uint64_t time;
} Engine;

void close_engine(Engine *e);
int engine_config_valid(const PluginConfig *config);
int open_engine(Engine *e, const char *path, const PluginConfig *config, unsigned sample_rate);
// Interleaved buffers sized by module input/output channels; NULL input is silence.
void render(Engine *e, float *out, const float *in, size_t frames);
#endif
