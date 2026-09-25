#include "player.h"
#include <math.h>
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
		size_t n = s->frames - s->time < frames ? (size_t)(s->frames - s->time) : frames;
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
	if (!s->sealed || !s->audio || s->describe) {
		s->error = "playback requires a prepared session";
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
	atomic_init(&p->position, s->time);
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
	p->tail = p->latency = (size_t)((frames * session_rate(s) + rate - 1) / rate);
	return p;
}

int player_start(Player *p) {
	int status = player_status(p);
	if (status && status != 2)
		return -1;
	atomic_store(&p->status, 0);
	ma_result result = ma_device_start(&p->device);
	if (result != MA_SUCCESS) {
		atomic_store(&p->status, status);
		p->session->error = ma_result_description(result);
	}
	return result != MA_SUCCESS;
}

int player_status(Player *p) {
	return atomic_load(&p->status);
}

double player_time(Player *p) {
	return (double)atomic_load(&p->position) / p->session->sample_rate;
}

int player_update(Player *p, Session *description, unsigned revision, int *mapping) {
	uint64_t earliest = atomic_load(&p->position) + p->session->sample_rate / 10;
	return session_update(p->session, description, earliest, revision, mapping);
}

void player_stop(Player *p) {
	atomic_store(&p->status, 2);
}

int player_pause(Player *p) {
	player_stop(p);
	ma_result result = ma_device_stop(&p->device);
	if (result != MA_SUCCESS)
		p->session->error = ma_result_description(result);
	return result != MA_SUCCESS;
}

int player_seek(Player *p, double seconds) {
	double frame = seconds * p->session->sample_rate;
	if (!isfinite(frame) || frame < 0 || frame >= 0x1p53) {
		p->session->error = "invalid position";
		return -1;
	}
	if (!player_status(p)) {
		p->session->error = "pause before seeking";
		return -1;
	}
	if (session_seek(p->session, (uint64_t)llround(frame)))
		return -1;
	p->tail = p->latency;
	atomic_store(&p->position, p->session->time);
	atomic_store(&p->status, 2);
	return 0;
}

void player_free(Player *p) {
	if (!p)
		return;
	player_stop(p);
	ma_device_uninit(&p->device);
	free(p);
}
