#include "daw.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int script(Session *s, Output *cfg, const char *source) {
	char path[] = "build/daw-test-XXXXXX";
	int fd = mkstemp(path); assert(fd >= 0);
	FILE *f = fdopen(fd, "w");
	assert(f && fputs(source, f) >= 0 && !fclose(f));
	int result = load_score(s, cfg, path);
	assert(!unlink(path)); return result;
}
static void bad_script(const char *source) {
	Session s = {0}; Output cfg;
	assert(script(&s, &cfg, source)); session_free(&s);
}
static void noop(void *p) { (void)p; }
static void set(void *p, size_t i, float v) { (void)i; *(float *)p = v; }
static void constant(void *p, const float **in, float **out, size_t n) {
	(void)in; for (size_t i = 0; i < n; ++i) out[0][i] = *(float *)p;
}
static void multiply(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i) out[0][i] = in[0][i] * *(float *)p;
}
static void add(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i) out[0][i] = in[0][i] + *(float *)p;
}
static int mock(Session *s, int kind, float value) {
	static const tibia_parameter param = {"value", "", 0, 4, 1, 0};
	static const tibia_info desc[] = {{0, 0, 1, &param}, {1, 0, 1, &param}};
	Node *n = s->nodes + s->nnodes;
	n->info = desc + (kind != 0); n->initial[0] = value; n->path = strdup("test");
	Engine *e = n->dsp;
	e->module = calloc(1, sizeof(*e->module)); e->instance = malloc(sizeof(float));
	assert(n->path && e->module && e->instance);
	*e->module = (TibiaModule){.fini = noop, .reset = noop, .set_parameter = set,
		.process = kind == 0 ? constant : kind == 1 ? multiply : add};
	return s->nnodes++;
}
static void pipeline(Session *s) {
	int source = mock(s, 0, 1), fx[] = {mock(s, 1, 2), mock(s, 2, .25f)};
	int tr = session_track(s, source, fx, 2, 0), master = session_track(s, -1, NULL, 0, 1);
	assert(tr >= 0 && master >= 0);
	assert(!session_set(s, tr, 1, -1));
	assert(!session_param(s, fx[0], 7, 0, .75f));
	assert(!session_param(s, fx[0], 7, 0, .5f)); // Last control wins.
	assert(!session_param(s, tr, 13, 0, .5f));
	assert(!session_param(s, tr, 17, 1, 1));
	assert(!session_param(s, master, 19, 0, .5f));
	assert(!session_end(s, 33));
}
static void test_pipeline(void) {
	Session a = {0}, b = {0}; float x[66], y[66];
	pipeline(&a); pipeline(&b);
	assert(!session_render(&a, x, 33));
	for (size_t i = 0; i < 33; ++i) assert(!session_render(&b, y + 2 * i, 1));
	assert(!memcmp(x, y, sizeof(x)));
	for (size_t i = 0; i < 33; ++i) {
		float expected = i < 7 ? 2.25f : i < 13 ? .75f : i < 19 ? .375f : .1875f;
		int c = i < 17 ? 0 : 1;
		assert(fabsf(x[2 * i + c] - expected) < 1e-6f && fabsf(x[2 * i + (c ^ 1)]) < 1e-6f);
	}
	assert(session_render(&a, x, 1) < 0);
	session_free(&a); session_free(&b);
	puts("OK: neutral output, serial effects, sample-accurate plugin/gain/pan/master controls, block invariance");
}
static void test_master(void) {
	Session s = {0}; float audio[64];
	int source = mock(&s, 0, 1), tr = session_track(&s, source, NULL, 0, 0);
	int fx = session_plugin(&s, "plugins/shape/build/plugin.so");
	assert(fx >= 0 && !session_set(&s, tr, 1, -1));
	assert(session_track(&s, -1, &fx, 1, 1) >= 0);
	assert(s.nodes[fx].dsp[0].instance != s.nodes[fx].dsp[1].instance);
	assert(!session_set(&s, fx, 2, .5f));
	assert(!session_param(&s, fx, 3, 0, .5f));
	assert(!session_param(&s, fx, 3, 2, 0));
	assert(!session_end(&s, 32) && !session_render(&s, audio, 32));
	for (int i = 0; i < 32; ++i)
		assert(fabsf(audio[2 * i] - (i < 3 ? tanhf(1) * powf(.5f, i + 1) : tanhf(.5f))) < 1e-6f && audio[2 * i + 1] == 0);
	session_free(&s); puts("OK: master effects use independent stereo state and shared automation");
}
static void test_wav(float value, float gain, Output cfg, float expected) {
	Session s = {0};
	int source = mock(&s, 0, value), tr = session_track(&s, source, NULL, 0, 0);
	assert(!session_set(&s, tr, 0, gain) && !session_set(&s, tr, 1, -1));
	assert(!session_end(&s, 1025) && !write_score(&s, &cfg, "build/export-test.wav"));
	FILE *f = fopen("build/export-test.wav", "rb"); unsigned char header[44];
	assert(f && fread(header, 1, sizeof(header), f) == sizeof(header));
	assert(!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVEfmt ", 8));
	assert(header[20] == (cfg.pcm16 ? 1 : 3) && header[22] == 2 && header[34] == (cfg.pcm16 ? 16 : 32));
	for (int i = 0; i < 1025; ++i) {
		if (cfg.pcm16) { int16_t pcm[2]; assert(fread(pcm, sizeof(pcm), 1, f) == 1);
			assert(abs(pcm[0] - (int)lrintf(expected * 32767)) <= 1 && pcm[1] == 0);
		} else { float pcm[2]; assert(fread(pcm, sizeof(pcm), 1, f) == 1);
			assert(fabsf(pcm[0] - expected) < 1e-6f && pcm[1] == 0);
		}
	}
	assert(fgetc(f) == EOF); fclose(f); session_free(&s);
}
static void test_plugins(void) {
	Engine echo = {0}; float input[1025] = {1}, out[1025]; input[200] = .25f;
	assert(!open_engine(&echo, "plugins/echo/build/plugin.so"));
	echo.module->set_parameter(echo.instance, 0, 1); // 1 ms -> 44 samples.
	echo.module->set_parameter(echo.instance, 3, 1);
	echo.module->set_parameter(echo.instance, 4, 0);
	echo.module->set_parameter(echo.instance, 5, 0);
	echo.module->set_parameter(echo.instance, 6, 0);
	const Event change = {100, 0, 2, {0}, 0};
	echo.events = &change; echo.count = 1;
	render(&echo, out, input, 1025);
	for (int i = 0; i < 1025; ++i) assert(out[i] == (i == 44 ? 1 : i == 288 ? .25f : 0));
	close_engine(&echo);
	Engine a = {0}, b = {0}; float x[2000], y[2000];
	assert(!open_engine(&a, "plugins/drums/build/plugin.so") && !open_engine(&b, "plugins/drums/build/plugin.so"));
	const Event hits[] = {{0, -1, 0, {0x90, 4, 127}, 0}, {100, 0, .5f, {0}, 1},
		{200, -1, 0, {0x90, 1, 100}, 2}, {800, -1, 0, {0x90, 2, 80}, 3}};
	a.events = b.events = hits; a.count = b.count = 4;
	render(&a, x, NULL, 2000);
	for (size_t i = 0; i < 2000;) {
		size_t n = 2000 - i < 257 ? 2000 - i : 257; render(&b, y + i, NULL, n); i += n;
	}
	assert(!memcmp(x, y, sizeof(x)));
	for (int i = 0; i < 100; ++i) {
		const uint8_t hit[] = {0x90, i % 7, 127}; a.module->midi_msg_in(a.instance, 0, hit);
	}
	a.module->set_parameter(a.instance, 0, 0); render(&a, x, NULL, 2000);
	for (int i = 0; i < 2000; ++i) assert(x[i] == 0 && isfinite(y[i]));
	close_engine(&a); close_engine(&b);
	puts("OK: echo timing/automation, deterministic percussion, voice bounds, live drum gain");
}
int main(void) {
	test_pipeline(); test_master(); test_plugins();
	test_wav(.25f, 1, (Output){0}, .25f);
	test_wav(.25f, .5f, (Output){0}, .125f);
	test_wav(2, 1, (Output){0}, 2); // Float WAV has no hidden clipping.
	test_wav(2, 1, (Output){.pcm16 = 1}, 1);
	test_wav(1e-35f, 1, (Output){.pcm16 = 1, .normalize = .94f}, .94f);
	test_wav(0, 1, (Output){.normalize = .94f}, 0);
	puts("OK: neutral float WAV, proportional gain, optional PCM16/normalization, silence and tiny values");
	Session s = {0}; Output cfg;
	assert(!load_score(&s, &cfg, "daw_test.janet"));
	assert(s.nnodes == 4 && s.nodes[0].count == 3003 && s.frames == 61 * SAMPLE_RATE);
	float audio[BLOCK * 2]; double energy = 0;
	while (s.time < s.frames) {
		size_t n = s.frames - s.time < BLOCK ? s.frames - s.time : BLOCK;
		assert(!session_render(&s, audio, n));
		for (size_t i = 0; i < n * 2; ++i) energy += audio[i] * audio[i];
	}
	assert(energy > 1 && s.nodes[0].dsp[0].next == 3003);
	session_free(&s);
	puts("OK: metadata, strict options/ranges, ownership, GC, 60 seconds of automation beyond 2048 events");
	bad_script("("); bad_script("unknown-binding");
	bad_script("(daw/end 1) (error \"expected failure after end\")");
	bad_script("(+ 1 2)");
	bad_script("(daw/plugin \"plugins/synth_mono/build/plugin.so\") (daw/end 1)");
	puts("OK: parse/runtime errors, missing end and orphan plugins fail cleanly");
	return 0;
}
