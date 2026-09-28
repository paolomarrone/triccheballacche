#ifndef SCORE_H
#define SCORE_H
#include "plugin.h"
#include "sequence.h"

enum { MAX_NODES = 128, MAX_TRACKS = 32, MAX_FX = 8 };

typedef struct {
	PluginConfig config;
	PluginInfo info;
	char *path, *name, *key;
	int *inputs, ninputs, channels;
	unsigned upstream, downstream; // Compiled track ancestry for solo routing.
	size_t event_count;            // Builder index for source annotations.
	int track;                     // Track index + 1, or zero for other nodes.
} Node;

typedef struct {
	int source, mixer, effects[MAX_FX], count;
} Track;

// Prepared musical data. No DSP instances, playback cursors, buffers or atomics.
typedef struct {
	Node nodes[MAX_NODES];
	Track tracks[MAX_TRACKS], master;
	int nnodes, ntracks, has_master, sealed, live;
	int output, has_output, order[MAX_NODES], norder;
	unsigned sample_rate;
	uint64_t frames;
	Sequence sequence;
	const char *error;
} Score;

unsigned score_rate(Score *s);
int score_plugin(Score *s, const char *path, const PluginConfig *config);
int score_set(Score *s, int id, int param, float value);
int score_through(Score *s, int source, int effect);
int score_mix(Score *s, const int *inputs, int count);
int score_output(Score *s, int source);
int score_track(Score *s, int source, const int *effects, int count, int master);
int score_cue(Score *s, Cue cue);
// Convenience entry points for finite events expressed in samples.
int score_param(Score *s, int id, uint64_t time, int param, float value);
int score_note(Score *s, int id, uint64_t time, uint64_t end, int pitch, int velocity);
int score_end(Score *s, uint64_t frames);
void score_remap(Score *s, const int *mapping);
void score_free(Score *s);
#endif
