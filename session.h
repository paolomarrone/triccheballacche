#ifndef SESSION_H
#define SESSION_H
#include "engine.h"
#include <stdatomic.h>

enum { MAX_NODES = 128, MAX_TRACKS = 32, MAX_FX = 8 };
enum { TRACK_MUTE = 1, TRACK_SOLO = 2 };

typedef struct {
	int node;
	float level; // Audio-owned fade for solo routing.
} Input;

typedef struct {
	Engine dsp[2]; // A mono effect on stereo audio uses independent L/R instances.
	char *path, *name;
	Input *inputs;
	int ninputs, channels;
	unsigned upstream, downstream; // Track ancestry, compiled once for solo routing.
	Event *events;
	size_t count, capacity, next;
	float values[2], defaults[2]; // Mixer gain/pan only; initial plugin values live in PluginConfig.
	int ncontrols, track;         // Track index + 1, or zero for other nodes.
} Node;

typedef struct {
	int source, mixer, effects[MAX_FX], count;
	atomic_int listen; // Editor audition flags, independent of score automation.
	float level;       // Audio-owned fade toward the current audible state.
} Track;

typedef struct {
	Node nodes[MAX_NODES];
	Modules *modules; // Borrowed from the DAW; session_free does not release it.
	Track tracks[MAX_TRACKS], master;
	int nnodes, ntracks, has_master, sealed;
	int output, has_output, order[MAX_NODES], norder;
	float *audio;         // One reusable block per node; shared outputs are rendered once.
	unsigned sample_rate; // Set before preparation; zero selects DEFAULT_SAMPLE_RATE.
	size_t frames, time;
	const char *error;
} Session;

unsigned session_rate(Session *s);
int session_plugin(Session *s, const char *path, const PluginConfig *config);
int session_set(Session *s, int id, int param, float value);
int session_through(Session *s, int source, int effect);
int session_mix(Session *s, const int *inputs, int count);
int session_output(Session *s, int source);
int session_track(Session *s, int source, const int *effects, int count, int master);
int session_param(Session *s, int id, size_t time, int param, float value);
int session_note(Session *s, int id, size_t time, size_t end, int pitch, int velocity);
int session_end(Session *s, size_t frames);
int session_render(Session *s, float *stereo, size_t frames);
// Safe during rendering; the caller owns session lifetime. Invalid requests leave it unchanged.
int session_listen(Session *s, int track, int flags);
// No audio callback may be using the session during rewind or stopped UI synchronization.
int session_rewind(Session *s);
void session_sync(Session *s);
void session_free(Session *s);
#endif
