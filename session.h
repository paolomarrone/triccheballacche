#ifndef SESSION_H
#define SESSION_H
#include "score.h"
#include <stdatomic.h>

enum { TRACK_MUTE = 1, TRACK_SOLO = 2 };

typedef struct {
	float level, *delay;
	size_t length, position; // Interleaved samples, prepared outside rendering.
} InputState;

typedef struct {
	uint64_t end;
	uint8_t velocity;
} ScheduledNote;

typedef struct {
	MidiBuffer messages;
	ScheduledNote notes[16 * 128];
	uint16_t order[16 * 128]; // Held notes in arrival order, also used when resuming a mono instrument.
	unsigned nnotes;
	uint8_t sustain[16], external_sustain[16];
	uint8_t external_notes[16 * 128];
	uint16_t external_channels;
} MidiInput;
typedef struct {
	Plugin dsp[2]; // Independent L/R instances for a mono effect on stereo audio.
	float values[2];
	MidiInput *midi_in;
	MidiBuffer *midi_out;
	InputState *inputs; // Per-edge solo fades and latency compensation.
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
	uint64_t frames, time, clock; // Musical extent/position and continuous processed samples.
	int playing;
	float *audio;
	_Atomic(Revision *) active, pending, retired;
	atomic_uint revision;
	uint64_t next_off;
	const char *error;
	MidiQueue *midi_in, *midi_out;      // Control producer/audio consumer, and the reverse.
	atomic_int midi_capture[MAX_NODES]; // Device subscriptions, read by the audio thread.
	int offline;                        // Changed by session_offline(), with audio stopped.
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
// Control thread, including during rendering. Messages enter at the next audio block.
int session_midi(Session *s, int node, const uint8_t *data, size_t size);
// Audio and device producer stopped. Discard queued device events and release external notes on resume.
void session_midi_panic(Session *s);
int session_render(Session *s, float *stereo, size_t frames);
// Process the same graph without advancing the sequence. MIDI and effect tails remain active.
int session_monitor(Session *s, float *stereo, size_t frames);
// Safe during rendering; the caller owns session lifetime.
int session_listen(Session *s, int track, int flags);
uint64_t session_frame(const Session *s, double seconds);
// Stop the callback first. Restore controls and held notes; reset DSP history.
int session_seek(Session *s, uint64_t from);
void session_sync(Session *s); // Audio stopped; apply controls and service plugin messages.
// Main thread: 1 requests a restart with audio stopped; -1 indicates a plugin error.
int session_poll(Session *s);
int session_restart(Session *s);
// Audio stopped. Changing mode may reset DSP history; latency compensation is rebuilt.
int session_offline(Session *s, int offline);
// Audio stopped. Presets apply to both instances of a duplicated mono effect.
int session_state(Session *s, int node, State *state, int write);
void session_free(Session *s);
#endif
