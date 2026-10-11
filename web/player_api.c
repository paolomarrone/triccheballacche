#include "player.h"
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Player *score_player(WebScore *score) {
	Player *p = player_new(&score->session);
	if (!p)
		fprintf(stderr, "Audio init: %s\n", score->session.error);
	return p;
}

const char *transport_update_web(Transport *t, Player *player, WebScore *next) {
	if (!next->view)
		return "Score projection missing";
	if (transport_update(t, &next->description, next->view, player_position(player)))
		return next->description.error;
	free(next->view);
	next->view = NULL;
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

int player_midi(Player *p, int node, const uint8_t *data, int size) {
	return player_status(p) || size < 0 ? -1 : session_midi(p->session, node, data, size);
}
int player_midi_capture(Player *p, int node, int enabled) {
	if (node < 0 || node >= p->session->nnodes || !p->session->nodes[node].midi_out)
		return -1;
	atomic_store(&p->session->midi_capture[node], !!enabled);
	return 0;
}
int player_midi_read(Player *p, uint8_t *data) {
	MidiPacket packet;
	if (!midi_pop(p->session->midi_out, &packet))
		return 0;
	// A small explicit wire layout, independent of C struct padding and pointer size.
	uint32_t header[] = {packet.node, (uint32_t)packet.frame, packet.frame >> 32};
	memcpy(data, header, sizeof(header));
	memcpy(data + sizeof(header), packet.data, packet.size);
	return packet.size;
}
void player_midi_panic(Player *p) {
	session_midi_panic(p->session);
}
