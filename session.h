#ifndef SESSION_H
#define SESSION_H
#include "engine.h"

enum { MAX_NODES = 128, MAX_TRACKS = 32, MAX_FX = 8 };
typedef struct {
	Engine dsp[2]; // A mono effect on stereo audio uses independent L/R instances.
	int nparams;
	uint64_t outputs;
	char *path;
	Event *events;
	size_t count, capacity, next;
	float initial[MAX_PARAMS], values[MAX_PARAMS];
	int attached;
} Node;
typedef struct { int source, mixer, effects[MAX_FX], count; } Track;
typedef struct {
	Node nodes[MAX_NODES];
	Track tracks[MAX_TRACKS], master;
	int nnodes, ntracks, has_master, sealed;
	size_t frames, time;
	const char *error;
} Session;

int session_plugin(Session *s, const char *path, const PluginConfig *config);
int session_set(Session *s, int id, int param, float value);
int session_track(Session *s, int source, const int *effects, int count, int master);
int session_param(Session *s, int id, size_t time, int param, float value);
int session_note(Session *s, int id, size_t time, size_t end, int pitch, int velocity);
int session_end(Session *s, size_t frames);
int session_render(Session *s, float *stereo, size_t frames);
void session_free(Session *s);
void session_pop(Session *s); // Roll back the last, still-unattached plugin.
#endif
