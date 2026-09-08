#define DAW_TEST
#include "daw.c"
#include <assert.h>
#include <unistd.h>

static void clear_score(void) {
	cleanup();
	memset(tracks, 0, sizeof(tracks));
	ntracks = nhits = 0; frames = 0; mix = send = NULL; seed = 0x706f6c70;
}
static int script(const char *source) {
	char path[] = "build/daw-test-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	FILE *f = fdopen(fd, "w");
	assert(f && fputs(source, f) >= 0 && !fclose(f));
	int result = load_score(path);
	assert(!unlink(path));
	return result;
}
static void bad_script(const char *source) {
	assert(script(source));
	clear_score();
}
int main(void) {
	assert(!load_score("daw_test.janet"));
	assert(ntracks == TRACKS && nhits == DRUMS && frames == SAMPLE_RATE);
	assert(tracks[0].engine.count == 6 && tracks[2].engine.count == EVENTS);
	assert(tracks[2].score[EVENTS - 1].value == 41);
	sort_score(tracks);
	Event *e = tracks[0].score;
	assert(e[0].time == 0 && e[0].midi[0] == 0x90);
	assert(e[1].value == 30 && e[2].value == 70);
	assert(e[3].midi[0] == 0x80 && e[4].midi[0] == 0x90);
	assert(e[5].time == SAMPLE_RATE);
	clear_score();
	puts("OK: Janet types/ranges, capacities, GC ownership, stable events, sealed score");

	assert(!load_score("examples/hello.janet"));
	assert(frames == 2 * SAMPLE_RATE && ntracks == 1 && tracks[0].engine.count == 14);
	assert(!write_score("build/hello-test.wav"));
	float peak = 0;
	for (size_t i = 0; i < frames; ++i) {
		assert(isfinite(mix[2 * i]) && mix[2 * i] == mix[2 * i + 1]);
		peak = fmaxf(peak, fabsf(mix[2 * i]));
	}
	assert(peak > .001f && tracks[0].engine.next == 14);
	FILE *f = fopen("build/hello-test.wav", "rb");
	unsigned char header[44];
	assert(f && fread(header, 1, sizeof(header), f) == sizeof(header));
	assert(!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVEfmt ", 8));
	assert(header[20] == 1 && header[22] == 2 && header[34] == 16);
	assert(!fseek(f, 0, SEEK_END) && ftell(f) == (long)(44 + frames * 4));
	fclose(f); clear_score();
	puts("OK: script -> C score -> finite stereo PCM16 WAV after Janet shutdown");
	assert(!script("(daw/export 0.01)"));
	assert(!write_score("build/silence-test.wav"));
	for (size_t i = 0; i < frames * 2; ++i) assert(mix[i] == 0);
	clear_score();
	assert(!script("(def t (daw/instrument \"examples/synth_mono/plugin.so\" {:gain 1e-35}))"
		"(daw/note t 0 0.01 60) (daw/export 0.02)"));
	assert(!write_score("build/quiet-test.wav"));
	clear_score();
	puts("OK: silence and near-zero normalization");
	bad_script("(");
	bad_script("unknown-binding");
	bad_script("(daw/export 1) (error \"expected failure after export\")");
	bad_script("(+ 1 2)");
	puts("OK: parse/runtime errors and missing export fail cleanly");
	return 0;
}
