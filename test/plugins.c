#include "posix/module.h"
#include "score_janet.h"
#include "script.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double measure(Plugin *e, int *crossings) {
	float out[257], previous = 0;
	double energy = 0;
	*crossings = 0;
	for (size_t left = DEFAULT_SAMPLE_RATE; left;) {
		size_t n = left < 257 ? left : 257;
		render_plugin(e, out, NULL, n);
		for (size_t i = 0; i < n; ++i) {
			assert(isfinite(out[i]));
			energy += out[i] * out[i];
			if (previous < 0 && out[i] >= 0)
				++*crossings;
			previous = out[i];
		}
		left -= n;
	}
	return energy / DEFAULT_SAMPLE_RATE;
}

static void test_synth(void) {
	Plugin e = {0};
	assert(!open_bundle(&e, "plugins/synth_mono/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	assert(e.config.midi == 0 && !e.config.input);
	assert(e.config.nparams == 39);
	assert(e.config.outputs & (UINT64_C(1) << 38));
	float warmup[4096];
	int crossings;
	assert(measure(&e, &crossings) < 1e-12);
	const uint8_t note[] = {0x90, 69, 100};
	e.dsp->api->midi_msg_in(e.dsp->instance, e.config.midi, note);
	const uint8_t bends[][3] = {{0xe0, 0, 64}, {0xe0, 0, 0}, {0xe0, 127, 127}, {0xe0, 0, 64}};
	const int expected[] = {440, 220, 880, 440};
	for (size_t i = 0; i < 4; ++i) {
		e.dsp->api->midi_msg_in(e.dsp->instance, e.config.midi, bends[i]);
		render_plugin(&e, warmup, NULL, 4096);
		assert(measure(&e, &crossings) > 1e-6);
		assert(abs(crossings - expected[i]) <= 2);
	}
	const uint8_t off[] = {0x90, 69, 0};
	e.dsp->api->midi_msg_in(e.dsp->instance, e.config.midi, off);
	render_plugin(&e, warmup, NULL, 4096);
	assert(measure(&e, &crossings) < 1e-12);
	close_plugin(&e);
	puts("OK: synth defaults, note on/off, pitch bend 440/220/880/440 Hz");
}

static int plugin(Score *score, const char *path) {
	char *binary;
	PluginConfig config;
	assert(!read_bundle(path, &binary, &config, NULL));
	int id = score_plugin(score, binary, &config);
	free(binary);
	assert(id >= 0);
	return id;
}

static void test_synth_blocks(void) {
	const size_t blocks[] = {BLOCK, 1, 43, 44, 45, 257};
	float reference[DEFAULT_SAMPLE_RATE], out[BLOCK * 2];
	for (size_t b = 0; b < sizeof(blocks) / sizeof(*blocks); ++b) {
		Score score = {0};
		int tone = plugin(&score, "plugins/synth_mono/build/plugin.perone");
		const int parameters[] = {26, 28, 29, 30, 31, 32};
		const float values[] = {300, 80, 100, 200, 20, 80};
		for (int i = 0; i < 6; ++i)
			assert(!score_param(&score, tone, 0, parameters[i], values[i]));
		assert(!score_note(&score, tone, 0, 17001, 60, 100));
		assert(!score_param(&score, tone, 12345, 26, 800));
		assert(!score_note(&score, tone, 18001, 20001, 64, 100));
		assert(score_output(&score, tone) >= 0 && !score_end(&score, DEFAULT_SAMPLE_RATE / 2));
		Session session = {0};
		assert(!session_activate(&session, &score));
		for (size_t pos = 0; pos < DEFAULT_SAMPLE_RATE / 2;) {
			size_t left = DEFAULT_SAMPLE_RATE / 2 - pos, n = left < blocks[b] ? left : blocks[b];
			assert(!session_render(&session, out, n));
			for (size_t i = 0; i < 2 * n; ++i)
				if (!b)
					reference[2 * pos + i] = out[i];
				else
					assert(out[i] == reference[2 * pos + i]);
			pos += n;
		}
		session_free(&session);
	}
	puts("OK: synth filter envelope and automation are independent of block size");
}

static void test_effect(void) {
	Plugin e = {0};
	assert(!open_bundle(&e, "plugins/tibia_test/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	float in[8193] = {1}, out[8193];
	render_plugin(&e, out, in, 8193);
	assert(out[0] > 0 && out[0] < 1);
	for (size_t i = 0; i < 8193; ++i)
		assert(isfinite(out[i]));
	e.dsp->api->set_parameter(e.dsp->instance, 3, 1);
	render_plugin(&e, out, in, 8193);
	for (size_t i = 0; i < 8193; ++i)
		assert(out[i] == in[i]);
	e.dsp->api->set_parameter(e.dsp->instance, 1, 1e6f);
	render_plugin(&e, out, NULL, 8193);
	close_plugin(&e);
	puts("OK: effect memory, audio input, bypass, delay bounds");
}

static void test_brickworks_effect(void) {
	Plugin e = {0};
	float in[512] = {1}, out[512];
	assert(!open_bundle(&e, "plugins/fx_svf/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	assert(e.config.input == 1 && e.config.midi == -1);
	assert(!e.dsp->api->get_parameter && !e.dsp->api->midi_msg_in);
	render_plugin(&e, out, in, 512);
	double energy = 0;
	for (size_t i = 0; i < 512; ++i) {
		assert(isfinite(out[i]));
		energy += out[i] * out[i];
	}
	assert(energy > 0);
	close_plugin(&e);
	puts("OK: unmodified Brickworks effect, audio buses and absent optional functions");
}

static void test_echo_drums(void) {
	Plugin echo = {0};
	float input[1025] = {1}, out[1025];
	input[200] = .25f;
	assert(!open_bundle(&echo, "plugins/echo/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	echo.dsp->api->set_parameter(echo.dsp->instance, 0, 1); // 1 ms -> 44 samples.
	echo.dsp->api->set_parameter(echo.dsp->instance, 3, 1);
	echo.dsp->api->set_parameter(echo.dsp->instance, 4, 0);
	echo.dsp->api->set_parameter(echo.dsp->instance, 5, 0);
	echo.dsp->api->set_parameter(echo.dsp->instance, 6, 0);
	render_plugin(&echo, out, input, 100);
	set_dsp(echo.dsp, 0, 2);
	render_plugin(&echo, out + 100, input + 100, 925);
	for (int i = 0; i < 1025; ++i)
		assert(out[i] == (i == 44 ? 1 : i == 288 ? .25f : 0));
	close_plugin(&echo);
	Session a = {0}, b = {0};
	float x[4000], y[4000];
	for (int i = 0; i < 2; ++i) {
		Score score = {0};
		int drums = plugin(&score, "plugins/drums/build/plugin.perone");
		assert(!score_note(&score, drums, 0, 1900, 4, 127));
		assert(!score_param(&score, drums, 100, 0, .5f));
		assert(!score_note(&score, drums, 200, 1900, 1, 100));
		assert(!score_note(&score, drums, 800, 1900, 2, 80));
		assert(score_output(&score, drums) >= 0 && !score_end(&score, 4000));
		assert(!session_activate(i ? &b : &a, &score));
	}
	assert(!session_render(&a, x, 2000));
	for (size_t i = 0; i < 2000;) {
		size_t n = 2000 - i < 257 ? 2000 - i : 257;
		assert(!session_render(&b, y + 2 * i, n));
		i += n;
	}
	assert(!memcmp(x, y, sizeof(x)));
	Plugin *drums = a.nodes[0].dsp;
	for (int i = 0; i < 100; ++i)
		midi_dsp(drums->dsp, drums->config.midi, (const uint8_t[]){0x90, i % 7, 127});
	set_dsp(drums->dsp, 0, 0);
	assert(!session_render(&a, x, 2000));
	for (int i = 0; i < 4000; ++i)
		assert(x[i] == 0 && isfinite(y[i]));
	session_free(&a);
	session_free(&b);
	puts("OK: echo timing/automation, deterministic percussion, voice bounds, live drum gain");
}

static void test_shape(void) {
	Plugin e = {0};
	float in[32], out[32];
	assert(!open_bundle(&e, "plugins/shape/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	for (int i = 0; i < 32; ++i)
		in[i] = 1;
	e.dsp->api->set_parameter(e.dsp->instance, 2, .5f);
	render_plugin(&e, out, in, 3);
	set_dsp(e.dsp, 0, .5f);
	set_dsp(e.dsp, 2, 0);
	render_plugin(&e, out + 3, in + 3, 29);
	for (int i = 0; i < 32; ++i)
		assert(fabsf(out[i] - (i < 3 ? tanhf(1) * powf(.5f, i + 1) : tanhf(.5f))) < 1e-6f);
	close_plugin(&e);
	puts("OK: waveshaper, filter state and automation");
}

int main(void) {
	test_synth();
	test_synth_blocks();
	test_effect();
	test_brickworks_effect();
	test_echo_drums();
	test_shape();
	Session s = {0};
	Output cfg;
	assert(!load_score(&s, &cfg, "test/plugins.janet"));
	float audio[BLOCK * 2];
	double energy = 0;
	while (s.time < s.frames) {
		size_t n = s.frames - s.time < BLOCK ? s.frames - s.time : BLOCK;
		assert(!session_render(&s, audio, n));
		for (size_t i = 0; i < n * 2; ++i)
			energy += audio[i] * audio[i];
	}
	assert(energy > 0);
	session_free(&s);
	puts("OK: real plugin metadata and Janet score integration");
	return 0;
}
