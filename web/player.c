#include "audio.h"
#include "host.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	ma_device device;
	Session *session;
	atomic_int status;
} Player;

static void callback(ma_device *device, void *out, const void *in, ma_uint32 frames) {
	(void)in;
	Player *p = device->pUserData;
	Session *s = p->session;
	memset(out, 0, frames * 2 * sizeof(float));
	if (atomic_load(&p->status))
		return;
	size_t n = s->frames - s->time;
	if (n > frames)
		n = frames;
	if (session_render(s, out, n)) {
		memset(out, 0, frames * 2 * sizeof(float));
		atomic_store(&p->status, -1);
	} else if (s->time == s->frames)
		atomic_store(&p->status, 1);
}

Player *player_new(Score *score) {
	if (score->output.normalize) {
		fputs("Normalization requires offline rendering\n", stderr);
		return NULL;
	}
	Player *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->session = &score->session;
	atomic_init(&p->status, 0);
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = session_rate(p->session);
	config.noClip = MA_TRUE;
	config.dataCallback = callback;
	config.pUserData = p;
	if (ma_device_init(NULL, &config, &p->device) != MA_SUCCESS) {
		free(p);
		return NULL;
	}
	return p;
}

int player_context(Player *p) {
	return p->device.webaudio.audioContext;
}

int player_node(Player *p) {
	return p->device.webaudio.audioWorklet;
}

int player_start(Player *p) {
	return ma_device_start(&p->device) != MA_SUCCESS;
}

int player_status(Player *p) {
	return atomic_load(&p->status);
}

void player_stop(Player *p) {
	atomic_store(&p->status, 2); // Silence any later callback while asynchronous teardown completes.
}

void player_free(Player *p) {
	ma_device_uninit(&p->device);
	free(p);
}
