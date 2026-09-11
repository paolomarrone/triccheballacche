#include "player.h"
#include "host.h"
#include <stdio.h>

Player *score_player(Score *score) {
	if (score->output.normalize) {
		fputs("Normalization requires offline rendering\n", stderr);
		return NULL;
	}
	Player *p = player_new(&score->session);
	if (!p)
		fprintf(stderr, "Audio init: %s\n", score->session.error);
	return p;
}

int player_context(Player *p) {
	return p->device.webaudio.audioContext;
}

int player_node(Player *p) {
	return p->device.webaudio.audioWorklet;
}
