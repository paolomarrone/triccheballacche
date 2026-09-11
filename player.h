#ifndef PLAYER_H
#define PLAYER_H
#include "audio.h"
#include "session.h"
#include <stdatomic.h>

typedef struct {
	ma_device device;
	Session *session;
	size_t tail; // Silence requested after the last score block, to flush the device queue.
	atomic_int status;
} Player;

// Borrows a fresh, sealed session. Free the player before freeing or reusing the session.
Player *player_new(Session *session);
int player_start(Player *player);
// 0: ready/rendering, 1: finished, -1: failed, 2: stopped.
int player_status(Player *player);
// Silences later callbacks; player_free waits for the device to stop.
void player_stop(Player *player);
void player_free(Player *player);
#endif
