#include "player.h"
#include "host.h"
#include <stdio.h>

Player *score_player(Score *score) {
	Player *p = player_new(&score->session);
	if (!p)
		fprintf(stderr, "Audio init: %s\n", score->session.error);
	return p;
}

const char *player_update_score(Player *player, Score *next, unsigned revision) {
	int mapping[MAX_NODES];
	if (player_update(player, &next->session, revision, mapping))
		return next->session.error;
	if (next->view)
		score_view_remap(next->view, mapping);
	return NULL;
}

int player_context(Player *p) {
	return p->device.webaudio.audioContext;
}

int player_node(Player *p) {
	return p->device.webaudio.audioWorklet;
}

void player_sync(Player *p) {
	if (player_status(p))
		session_sync(p->session);
}
