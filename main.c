#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_DECODING
#define MA_NO_NULL
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "loader.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SAMPLE_RATE = 44100, BLOCK = 512 };
typedef struct {
	size_t time;
	int parameter; // -1: MIDI, otherwise parameter index
	float value;
	uint8_t midi[3];
} Event;
typedef struct {
	TibiaModule *module;
	void *instance, *memory;
	const Event *events;
	size_t count, next, time;
} Engine;

static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }

static void close_engine(Engine *e) {
	if (e->instance) e->module->fini(e->instance);
	free(e->memory);
	free(e->instance);
	tibia_loader_unload(e->module);
}

static int open_engine(Engine *e, const char *path) {
	if (!(e->module = tibia_loader_load(path))) return -1;
	if (!(e->instance = e->module->new())) return -1;
	e->module->init(e->instance, NULL);
	e->module->set_sample_rate(e->instance, SAMPLE_RATE);
	size_t size = e->module->mem_req(e->instance);
	if (size && !(e->memory = malloc(size))) return -1;
	e->module->mem_set(e->instance, e->memory);
	e->module->reset(e->instance);
	return 0;
}

// Events are chronological; mono buffers can be processed directly.
static void render(Engine *e, float *out, const float *in, size_t frames) {
	static const float silence[BLOCK];
	while (frames) {
		while (e->next < e->count && e->events[e->next].time <= e->time) {
			const Event *v = &e->events[e->next++];
			if (v->parameter >= 0)
				e->module->set_parameter(e->instance, v->parameter, v->value);
			else if (e->module->midi_msg_in)
				e->module->midi_msg_in(e->instance, 0, v->midi);
		}
		size_t n = frames < BLOCK ? frames : BLOCK;
		if (e->next < e->count && e->events[e->next].time - e->time < n)
			n = e->events[e->next].time - e->time;
		const float *input = in ? in : silence;
		e->module->process(e->instance, &input, &out, n);
		e->time += n;
		frames -= n;
		out += n;
		if (in) in += n;
	}
}

static void callback(ma_device *device, void *out, const void *in, ma_uint32 n) {
	render(device->pUserData, out, in, n);
}

int main(int argc, char **argv) {
	if (argc < 2 || argc > 4 || (argc == 3 && strcmp(argv[2], "--input")) ||
	    (argc == 4 && strcmp(argv[2], "--wav"))) {
		fprintf(stderr, "Usage: %s plugin.so [--input | --wav output.wav]\n", argv[0]);
		return 1;
	}
	const Event demo[] = {
		{0, -1, 0, {0x90, 60, 100}},
		{SAMPLE_RATE, -1, 0, {0x80, 60, 0}},
		{2 * SAMPLE_RATE, -1, 0, {0x90, 64, 100}},
		{3 * SAMPLE_RATE, -1, 0, {0x90, 64, 0}},
		{4 * SAMPLE_RATE, -1, 0, {0x90, 67, 100}},
		{5 * SAMPLE_RATE, -1, 0, {0xe0, 0, 0x60}},
		{6 * SAMPLE_RATE, -1, 0, {0xe0, 0, 0x40}},
		{7 * SAMPLE_RATE, -1, 0, {0x80, 67, 0}}
	};
	int result = 1, capture = argc == 3;
	Engine e = {.events = demo, .count = capture ? 0 : sizeof(demo) / sizeof(*demo)};
	if (open_engine(&e, argv[1])) { fputs("Plugin initialization failed\n", stderr); goto done; }
	if (argc == 4) {
		ma_encoder encoder;
		ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, SAMPLE_RATE);
		if (ma_encoder_init_file(argv[3], &cfg, &encoder) != MA_SUCCESS) goto done;
		result = 0;
		for (size_t left = 8 * SAMPLE_RATE; left;) {
			float buffer[BLOCK];
			size_t n = left < BLOCK ? left : BLOCK;
			ma_uint64 written;
			render(&e, buffer, NULL, n);
			if (ma_encoder_write_pcm_frames(&encoder, buffer, n, &written) != MA_SUCCESS || written != n) {
				result = 1; break;
			}
			left -= n;
		}
		ma_encoder_uninit(&encoder);
		goto done;
	}
	ma_device device;
	ma_device_config cfg = ma_device_config_init(capture ? ma_device_type_duplex : ma_device_type_playback);
	cfg.playback.format = cfg.capture.format = ma_format_f32;
	cfg.playback.channels = cfg.capture.channels = 1;
	cfg.sampleRate = SAMPLE_RATE;
	cfg.dataCallback = callback;
	cfg.pUserData = &e;
	ma_result err = ma_device_init(NULL, &cfg, &device);
	if (err != MA_SUCCESS) { fprintf(stderr, "Audio init: %s\n", ma_result_description(err)); goto done; }
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	err = ma_device_start(&device);
	if (err == MA_SUCCESS) {
		printf("Playing (%s); Ctrl-C to stop.\n", ma_get_backend_name(device.pContext->backend));
		for (unsigned ticks = 0; !stopped && (capture || ticks < 80); ++ticks) ma_sleep(100);
		result = 0;
	} else fprintf(stderr, "Audio start: %s\n", ma_result_description(err));
	ma_device_uninit(&device);
done:
	close_engine(&e);
	if (result) fputs("Playback/render failed\n", stderr);
	return result;
}
