#ifndef PLAYER_H
#define PLAYER_H
#include "audio.h"
#include "session.h"
#include <stdatomic.h>

typedef struct {
	ma_device device;
	Session *session;
	size_t tail, latency; // Silence requested after the last score block, to flush the device queue.
	atomic_int status, running, monitoring;
	_Atomic uint64_t position, clock;
} Player;

// Borrows a prepared session. Pause before replacing it at the same address; free before destroying it.
Player *player_new(Session *session);
int player_start(Player *player);
// Enable continuous audio without changing the transport, including after the score ends.
int player_monitor(Player *player);
int player_active(Player *player);
// Transport: 0 playing, 1 finished, 2 stopped, -1 failed. Audio may still process at rest.
int player_status(Player *player);
// Rendered seconds, published by the audio callback; device latency is not subtracted.
double player_time(Player *player);
uint64_t player_position(Player *player);
// Continuous audio samples, independent of musical position and seeking.
uint64_t player_clock(Player *player);
double player_audio_time(Player *player);
// Silences later callbacks; player_free waits for the device to stop.
void player_stop(Player *player);
// Pause the device and wait for callbacks to finish. Web callers also await AudioContext.suspend().
int player_pause(Player *player);
// Caller has quiesced audio. Seek in seconds, retaining the device and DSP instances; remains stopped.
int player_seek(Player *player, double seconds);
void player_free(Player *player);
#endif
