#include "audio.h"
#include "script.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stopped;

static void stop(int sig) {
	(void)sig;
	stopped = 1;
}

static void callback(ma_device *device, void *out, const void *in, ma_uint32 n) {
	render(device->pUserData, out, in, n);
}

int main(int argc, char **argv) {
	if (argc < 2 || argc > 4 || (argc == 3 && strcmp(argv[2], "--input")) || (argc == 4 && strcmp(argv[2], "--wav"))) {
		fprintf(stderr, "Usage: %s plugin.perone [--input | --wav output.wav]\n", argv[0]);
		return 1;
	}
	const Event demo[] = {{0, -1, 0, {0x90, 60, 100}, 0}, {SAMPLE_RATE, -1, 0, {0x80, 60, 0}, 0},
	    {2 * SAMPLE_RATE, -1, 0, {0x90, 64, 100}, 0}, {3 * SAMPLE_RATE, -1, 0, {0x90, 64, 0}, 0},
	    {4 * SAMPLE_RATE, -1, 0, {0x90, 67, 100}, 0}, {5 * SAMPLE_RATE, -1, 0, {0xe0, 0, 0x60}, 0},
	    {6 * SAMPLE_RATE, -1, 0, {0xe0, 0, 0x40}, 0}, {7 * SAMPLE_RATE, -1, 0, {0x80, 67, 0}, 0}};
	int result = 1, capture = argc == 3;
	Engine e = {.events = demo, .count = capture ? 0 : sizeof(demo) / sizeof(*demo)};
	if (open_bundle(&e, argv[1])) {
		fputs("Plugin initialization failed\n", stderr);
		goto done;
	}
	if (capture && !e.module->config.input) {
		fputs("Plugin has no audio input\n", stderr);
		goto done;
	}
	if (argc == 4) {
		ma_encoder encoder;
		ma_encoder_config cfg =
		    ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, e.module->config.output, SAMPLE_RATE);
		if (ma_encoder_init_file(argv[3], &cfg, &encoder) != MA_SUCCESS)
			goto done;
		result = 0;
		for (size_t left = 8 * SAMPLE_RATE; left;) {
			float buffer[BLOCK * 2];
			size_t n = left < BLOCK ? left : BLOCK;
			ma_uint64 written;
			render(&e, buffer, NULL, n);
			if (ma_encoder_write_pcm_frames(&encoder, buffer, n, &written) != MA_SUCCESS || written != n) {
				result = 1;
				break;
			}
			left -= n;
		}
		ma_encoder_uninit(&encoder);
		goto done;
	}
	ma_device device;
	ma_device_config cfg = ma_device_config_init(capture ? ma_device_type_duplex : ma_device_type_playback);
	cfg.playback.format = cfg.capture.format = ma_format_f32;
	cfg.playback.channels = e.module->config.output;
	cfg.capture.channels = e.module->config.input;
	cfg.sampleRate = SAMPLE_RATE;
	cfg.dataCallback = callback;
	cfg.pUserData = &e;
	ma_result err = ma_device_init(NULL, &cfg, &device);
	if (err != MA_SUCCESS) {
		fprintf(stderr, "Audio init: %s\n", ma_result_description(err));
		goto done;
	}
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	err = ma_device_start(&device);
	if (err == MA_SUCCESS) {
		printf("Playing (%s); Ctrl-C to stop.\n", ma_get_backend_name(device.pContext->backend));
		for (unsigned ticks = 0; !stopped && (capture || ticks < 80); ++ticks)
			nanosleep(&(struct timespec){.tv_nsec = 100000000}, NULL);
		result = 0;
	} else
		fprintf(stderr, "Audio start: %s\n", ma_result_description(err));
	ma_device_uninit(&device);
done:
	close_engine(&e);
	if (result)
		fputs("Playback/render failed\n", stderr);
	return result;
}
