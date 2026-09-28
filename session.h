#ifndef SESSION_H
#define SESSION_H
#include "score.h"
#include <stdatomic.h>

enum { TRACK_MUTE = 1, TRACK_SOLO = 2 };

typedef struct {
	Plugin dsp[2];            // Independent L/R instances for a mono effect on stereo audio.
	float values[2], *inputs; // Mixer controls and per-edge solo fades.
} NodeState;

typedef struct {
	atomic_int listen;
	float level;
} TrackState;

typedef struct {
	Score score;
	Sequencer cursor;
	uint64_t at;
	unsigned revision;
} Revision;

typedef struct {
	NodeState nodes[MAX_NODES];
	TrackState tracks[MAX_TRACKS];
	Modules *modules; // Borrowed; instances retain their modules independently.
	int nnodes, ntracks;
	unsigned sample_rate;
	uint64_t frames, time;
	float *audio;
	_Atomic(Revision *) active, pending, retired;
	atomic_uint revision;
	uint64_t (*held)[128]; // Note-off obligations survive a musical revision.
	uint64_t next_off;
	const char *error;
} Session;

// The control thread may borrow this until collect/free; audio owns the active cursor.
static inline const Score *session_score(const Session *s) {
	const Revision *r = atomic_load(&s->active);
	return r ? &r->score : NULL;
}
unsigned session_rate(Session *s);
// Move a prepared score into an empty session on success. Failure leaves score owned by the caller.
int session_activate(Session *s, Score *score);
// Move a prepared revision on success; keep DSP instances and held notes. Mapping receives old node IDs.
int session_update(Session *s, Score *score, uint64_t earliest, unsigned revision, int *mapping);
// Free retired musical data on the control thread.
void session_collect(Session *s);
// Discard an unplayed revision after stopping the callback.
void session_cancel(Session *s);
int session_render(Session *s, float *stereo, size_t frames);
// Safe during rendering; the caller owns session lifetime.
int session_listen(Session *s, int track, int flags);
uint64_t session_frame(const Session *s, double seconds);
// Stop the callback first. Restore controls and held notes; reset DSP history.
int session_seek(Session *s, uint64_t from);
void session_sync(Session *s);
void session_free(Session *s);
#endif
