#include "player.h"
#include <stdlib.h>
#include <string.h>

static void callback(ma_device *device, void *out, const void *in, ma_uint32 frames) {
	(void)in;
	Player *p = device->pUserData;
	Session *s = p->session;
	memset(out, 0, frames * 2 * sizeof(float));
	if (atomic_load(&p->status))
		return;
	int status = 0;
	if (s->time == s->frames) {
		if (p->tail > frames)
			p->tail -= frames;
		else
			status = 1;
	} else {
		size_t n = s->frames - s->time;
		if (n > frames)
			n = frames;
		if (session_render(s, out, n)) {
			memset(out, 0, frames * 2 * sizeof(float));
			status = -1;
		}
	}
	atomic_store(&p->position, s->time);
	int expected = 0;
	atomic_compare_exchange_strong(&p->status, &expected, status);
}

Player *player_new(Session *s) {
	if (!s->sealed || s->time) {
		s->error = "playback requires a fresh, sealed session";
		return NULL;
	}
	s->error = NULL;
	Player *p = calloc(1, sizeof(*p));
	if (!p) {
		s->error = "out of memory";
		return NULL;
	}
	p->session = s;
	atomic_init(&p->status, 0);
	atomic_init(&p->position, 0);
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = 2;
	config.sampleRate = session_rate(s);
	config.noClip = MA_TRUE;
	config.noFixedSizedCallback = MA_TRUE;
	config.dataCallback = callback;
	config.pUserData = p;
	ma_result result = ma_device_init(NULL, &config, &p->device);
	if (result != MA_SUCCESS) {
		s->error = ma_result_description(result);
		free(p);
		return NULL;
	}
	// Let the queued audio reach the device before reporting completion. Some backends pause on uninit.
	uint64_t frames = (uint64_t)p->device.playback.internalPeriodSizeInFrames * p->device.playback.internalPeriods;
	unsigned rate = p->device.playback.internalSampleRate;
	p->tail = (size_t)((frames * session_rate(s) + rate - 1) / rate);
	return p;
}

int player_start(Player *p) {
	if (player_status(p))
		return -1;
	ma_result result = ma_device_start(&p->device);
	if (result != MA_SUCCESS)
		p->session->error = ma_result_description(result);
	return result != MA_SUCCESS;
}

int player_status(Player *p) {
	return atomic_load(&p->status);
}

double player_time(Player *p) {
	return (double)atomic_load(&p->position) / p->session->sample_rate;
}

void player_stop(Player *p) {
	atomic_store(&p->status, 2);
}

void player_free(Player *p) {
	if (!p)
		return;
	player_stop(p);
	ma_device_uninit(&p->device);
	free(p);
}
