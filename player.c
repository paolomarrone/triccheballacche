#include "player.h"
#include <stdlib.h>
#include <string.h>

static void callback(ma_device *device, void *out, const void *in, ma_uint32 frames) {
	(void)in;
	Player *p = device->pUserData;
	Session *s = p->session;
	memset(out, 0, frames * 2 * sizeof(float));
	int previous = atomic_load(&p->status);
	if (!atomic_load(&p->running) || previous < 0)
		return;
	int status = previous;
	size_t n = 0;
	if (!previous && s->time == s->frames) {
		if (p->tail > frames)
			p->tail -= frames;
		else
			status = 1;
	} else if (!previous) {
		n = s->frames - s->time < frames ? (size_t)(s->frames - s->time) : frames;
		if (session_render(s, out, n))
			status = -1;
	}
	if (status >= 0 && atomic_load(&p->monitoring) && n < frames &&
	    session_monitor(s, (float *)out + 2 * n, frames - n))
		status = -1;
	if (status < 0)
		memset(out, 0, frames * 2 * sizeof(float));
	atomic_store(&p->position, s->time);
	atomic_store(&p->clock, s->clock);
	atomic_compare_exchange_strong(&p->status, &previous, status);
}

Player *player_new(Session *s) {
	if (!s->audio) {
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
	atomic_init(&p->status, 2);
	atomic_init(&p->running, 0);
	atomic_init(&p->monitoring, 0);
	atomic_init(&p->position, s->time);
	atomic_init(&p->clock, s->clock);
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

static int start(Player *p, int status) {
	int previous = atomic_exchange(&p->status, status);
	atomic_store(&p->running, 1);
	ma_result result = ma_device_start(&p->device);
	if (result != MA_SUCCESS) {
		atomic_store(&p->running, 0);
		atomic_store(&p->status, previous);
		p->session->error = ma_result_description(result);
	}
	return result != MA_SUCCESS;
}

int player_start(Player *p) {
	int status = player_status(p);
	return !status ? 0 : status == 2 ? start(p, 0) : -1;
}

int player_monitor(Player *p) {
	if (player_status(p) < 0)
		return -1;
	atomic_store(&p->monitoring, 1);
	return atomic_load(&p->running) ? 0 : start(p, 2);
}

int player_active(Player *p) {
	return atomic_load(&p->running) && player_status(p) >= 0;
}

int player_status(Player *p) {
	return atomic_load(&p->status);
}

double player_time(Player *p) {
	return (double)player_position(p) / p->session->sample_rate;
}

uint64_t player_position(Player *p) {
	return atomic_load(&p->position);
}

uint64_t player_clock(Player *p) {
	return atomic_load(&p->clock);
}

double player_audio_time(Player *p) {
	return (double)player_clock(p) / p->session->sample_rate;
}

void player_stop(Player *p) {
	atomic_store(&p->running, 0);
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
	uint64_t frame = session_frame(p->session, seconds);
	if (frame == UINT64_MAX) {
		p->session->error = "invalid position";
		return -1;
	}
	if (atomic_load(&p->running)) {
		p->session->error = "pause before seeking";
		return -1;
	}
	if (session_seek(p->session, frame))
		return -1;
	p->tail = p->latency;
	atomic_store(&p->position, p->session->time);
	atomic_store(&p->clock, p->session->clock);
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
