#ifndef SESSION_H
#define SESSION_H
#include "engine.h"
#include "sequence.h"
#include <stdatomic.h>

enum { MAX_NODES = 128, MAX_TRACKS = 32, MAX_FX = 8 };
enum { TRACK_MUTE = 1, TRACK_SOLO = 2 };

typedef struct {
	int node;
	float level; // Audio-owned fade for solo routing.
} Input;

typedef struct {
	Engine dsp[2]; // A mono effect on stereo audio uses independent L/R instances.
	char *path, *name, *key;
	Input *inputs;
	int ninputs, channels;
	unsigned upstream, downstream; // Track ancestry, compiled once for solo routing.
	Event *events;
	size_t count, capacity, next;
	float values[2], defaults[2]; // Mixer gain/pan only; initial plugin values live in PluginConfig.
	int track;                    // Track index + 1, or zero for other nodes.
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
	uint64_t frames, time;
	int describe; // Leave a sealed description; session_activate creates the audio resources.
	_Atomic(Sequence *) sequence;
	_Atomic(Sequence *) pending, retired;
	atomic_uint revision;
	uint64_t (*held)[128]; // Note-off obligations survive a musical revision.
	uint64_t next_off;
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
int session_end(Session *s, uint64_t frames);
int session_activate(Session *s);
// One control thread publishes a sealed description's sequence, transferring ownership on success.
// mapping receives description-to-runtime node IDs. Audio applies the sequence at its boundary.
int session_update(Session *s, Session *description, uint64_t earliest, unsigned revision, int *mapping);
// Release the previous sequence on the control thread after the audio has retired it.
void session_collect(Session *s);
// Discard an unplayed revision after stopping the audio callback.
void session_cancel(Session *s);
int session_render(Session *s, float *stereo, size_t frames);
// Safe during rendering; the caller owns session lifetime. Invalid requests leave it unchanged.
int session_listen(Session *s, int track, int flags);
// Read-only conversion to an absolute sample; UINT64_MAX means outside the prepared score.
uint64_t session_frame(const Session *s, double seconds);
// No audio callback may be using the session during seek or stopped UI synchronization.
// Seek to an absolute sample, restoring controls and held notes. DSP history is reset.
int session_seek(Session *s, uint64_t from);
void session_sync(Session *s);
void session_free(Session *s);
#endif
