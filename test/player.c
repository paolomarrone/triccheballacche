#include "player.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Replace only device I/O: use the real player, CLI, Janet, DSP, mixer and WAV exporter.
static ma_result device_init(ma_context *, const ma_device_config *, ma_device *);
static ma_result device_start(ma_device *);
static void device_free(ma_device *);
static ma_bool32 device_started(const ma_device *);
static int tick(const struct timespec *, struct timespec *);
#define ma_device_init device_init
#define ma_device_start device_start
#define ma_device_uninit device_free
#define ma_device_is_started device_started
#define nanosleep tick
#include "../player.c"
#define main cli_main
#include "../posix/daw_main.c"
#undef main
#undef nanosleep
#undef ma_device_is_started
#undef ma_device_uninit
#undef ma_device_start
#undef ma_device_init

enum { NORMAL, INIT_FAIL, START_FAIL, INTERRUPT, DISCONNECT };
static int mode, opened, closed, started;
static ma_device *device;
static size_t emitted, expected_frames, queue_frames;
static float *expected;

static ma_result device_init(ma_context *context, const ma_device_config *config, ma_device *out) {
	assert(!context && !device);
	assert(config->deviceType == ma_device_type_playback && config->playback.format == ma_format_f32);
	assert(config->playback.channels == 2 && config->noClip && config->noFixedSizedCallback);
	if (mode == INIT_FAIL)
		return MA_NO_DEVICE;
	*out = (ma_device){.pUserData = config->pUserData, .onData = config->dataCallback};
	out->playback.internalSampleRate = 48000;
	out->playback.internalPeriodSizeInFrames = 128;
	out->playback.internalPeriods = 3;
	queue_frames = (384ULL * config->sampleRate + 47999) / 48000;
	device = out;
	emitted = 0;
	++opened;
	return MA_SUCCESS;
}

static ma_result device_start(ma_device *p) {
	assert(p == device);
	if (mode == START_FAIL)
		return MA_ERROR;
	started = 1;
	return MA_SUCCESS;
}

static void pump(size_t frames) {
	float audio[8192];
	assert(frames <= 4096);
	for (size_t i = 0; i < frames * 2; ++i)
		audio[i] = 123;
	device->onData(device, audio, NULL, (ma_uint32)frames);
	if (expected)
		for (size_t i = 0; i < frames * 2; ++i) {
			size_t index = emitted * 2 + i;
			assert(audio[i] == (index < expected_frames * 2 ? expected[index] : 0));
		}
	emitted += frames;
}

static void device_free(ma_device *p) {
	assert(p == device);
	Player *player = p->pUserData;
	Session *s = player->session;
	assert(s->sealed && s->nnodes && s->nodes[0].dsp[0].dsp);
	// A callback that arrives during teardown must neither render nor access freed DSPs.
	size_t time = s->time;
	float audio[2] = {123, 123};
	p->onData(p, audio, NULL, 1);
	assert(s->time == time && audio[0] == 0 && audio[1] == 0);
	device = NULL;
	started = 0;
	++closed;
}

static ma_bool32 device_started(const ma_device *p) {
	assert(p == device);
	return started;
}

static int tick(const struct timespec *delay, struct timespec *remaining) {
	(void)delay;
	(void)remaining;
	if (mode == INTERRUPT)
		raise(SIGINT);
	else if (mode == DISCONNECT)
		started = 0;
	else
		pump(128);
	return 0;
}

static void reference(const char *path, unsigned rate) {
	Session s = {.sample_rate = rate};
	Output output;
	assert(!load_score(&s, &output, path));
	expected_frames = s.frames;
	// Playback uses the mix before export normalization or PCM16 conversion.
	assert(!write_score(&s, &(Output){0}, "build/player-reference.wav"));
	session_free(&s);
	FILE *file = fopen("build/player-reference.wav", "rb");
	unsigned char header[44];
	assert(file && fread(header, 1, sizeof(header), file) == sizeof(header));
	assert(header[20] == 3 && header[22] == 2 && header[34] == 32);
	free(expected);
	expected = malloc(expected_frames * 2 * sizeof(float));
	assert(expected && fread(expected, sizeof(float) * 2, expected_frames, file) == expected_frames);
	assert(fgetc(file) == EOF && !fclose(file));
	assert(!unlink("build/player-reference.wav"));
}

static void test_pcm(void) {
	const char *paths[] = {"test/playback.janet", "test/schedule.janet"};
	const unsigned rates[] = {44100, 48000};
	const size_t blocks[] = {1, 17, 128, 511, 4096};
	for (size_t i = 0; i < 2; ++i)
		for (size_t j = 0; j < 2; ++j) {
			reference(paths[i], rates[j]);
			Session s = {.sample_rate = rates[j]};
			Output output;
			assert(!load_score(&s, &output, paths[i]));
			Player *p = player_new(&s);
			assert(p && !player_start(p));
			for (size_t block = 0; !player_status(p); ++block) {
				assert(block < 1000);
				// A short final block must not hide a truncated tail behind a large silent buffer.
				pump(i == 0 ? 128 : blocks[block % 5]);
			}
			assert(player_status(p) == 1 && s.time == s.frames);
			assert(emitted >= s.frames + queue_frames); // Last audio must leave the simulated device queue.
			assert(player_start(p));
			pump(128); // Every subsequent callback stays silent.
			player_free(p);
			session_free(&s);
		}
	free(expected);
	expected = NULL;
	puts("OK: native player PCM matches WAV at 44.1/48 kHz; variable blocks, final silence and queue completion");
}

static void test_lifecycle(void) {
	Session s = {0};
	Output output;
	assert(!player_new(&s) && s.error);
	assert(!load_score(&s, &output, "test/playback.janet"));
	mode = INIT_FAIL;
	assert(!player_new(&s) && s.error && !device);
	mode = START_FAIL;
	Player *p = player_new(&s);
	assert(p && player_start(p) && s.error && !s.time);
	player_free(p);
	mode = NORMAL;
	p = player_new(&s);
	assert(p);
	player_stop(p);
	player_stop(p);
	pump(128);
	assert(player_status(p) == 2 && !s.time && player_start(p));
	player_free(p);
	p = player_new(&s);
	assert(p && !player_start(p));
	pump(128);
	size_t time = s.time;
	assert(time == 128);
	player_stop(p);
	pump(128);
	assert(s.time == time && player_status(p) == 2 && player_start(p));
	player_free(p);
	session_free(&s);
	assert(!load_score(&s, &output, "test/playback.janet"));
	p = player_new(&s);
	assert(p && !player_start(p));
	s.nodes[s.tracks[0].mixer].values[0] = INFINITY;
	float audio[256];
	device->onData(device, audio, NULL, 128);
	assert(player_status(p) == -1 && s.error);
	for (size_t i = 0; i < 256; ++i)
		assert(audio[i] == 0);
	player_free(p);
	s.time = 1;
	assert(!player_new(&s) && s.error);
	session_free(&s);
	player_free(NULL);
	assert(opened == closed);
	puts("OK: player validation, init/start/render failures, stop and device-before-session cleanup");
}

static void test_cli(void) {
	char *args[] = {"daw", "--play", "test/playback.janet", "48000"};
	reference(args[2], 48000);
	assert(!cli_main(4, args) && emitted >= expected_frames + queue_frames);
	free(expected);
	expected = NULL;
	for (mode = INIT_FAIL; mode <= DISCONNECT; ++mode)
		assert(!!cli_main(3, args) == (mode != INTERRUPT));
	mode = NORMAL;
	int before = opened;
	args[3] = "bad-rate";
	assert(cli_main(4, args));
	assert(cli_main(2, args));
	char path[] = "build/player-score-XXXXXX";
	int fd = mkstemp(path);
	FILE *file = fdopen(fd, "w");
	assert(fd >= 0 && file);
	assert(fputs("(def s (daw/plugin \"build/fixture.perone\" {:gain 0.25})) (daw/track s) "
	             "(daw/end 0.05003 {:format :pcm16 :normalize 0.9})",
	           file) >= 0);
	assert(!fclose(file));
	args[2] = path;
	reference(path, 44100);
	assert(!cli_main(3, args) && emitted >= expected_frames + queue_frames);
	free(expected);
	expected = NULL;
	assert(!unlink(path));
	assert(cli_main(3, args)); // Missing score also fails before opening a device.
	assert(opened == before + 1 && opened == closed);
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	puts("OK: --play CLI, rate/argument validation, export-only options, cancellation and device errors");
}

int main(void) {
	test_pcm();
	test_lifecycle();
	test_cli();
	return 0;
}
