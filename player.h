#ifndef PLAYER_H
#define PLAYER_H
#include "audio.h"
#include "session.h"
#include <stdatomic.h>

typedef struct {
	ma_device device;
	Session *session;
	size_t tail, latency; // Silence requested after the last score block, to flush the device queue.
	atomic_int status;
	_Atomic uint64_t position;
} Player;

// Borrows a fresh, sealed session. Pause before replacing it at the same address; free before destroying it.
Player *player_new(Session *session);
int player_start(Player *player);
// 0: ready/rendering, 1: finished, -1: failed, 2: stopped.
int player_status(Player *player);
// Rendered seconds, published by the audio callback; device latency is not subtracted.
double player_time(Player *player);
// Publish a revision using the sample clock, with a 100 ms preparation margin.
int player_update(Player *player, Session *description, unsigned revision, int *mapping);
// Silences later callbacks; player_free waits for the device to stop.
void player_stop(Player *player);
// Pause the device and wait for callbacks to finish. Web callers also await AudioContext.suspend().
int player_pause(Player *player);
// Caller has quiesced audio. Restore the prepared score, keeping the device and all DSP instances.
int player_rewind(Player *player);
void player_free(Player *player);
#endif
