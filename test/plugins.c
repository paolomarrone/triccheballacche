#include "module.h"
#include "daw.h"
#include "script.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double measure(Engine *e, int *crossings) {
	float out[257], previous = 0;
	double energy = 0;
	*crossings = 0;
	for (size_t left = DEFAULT_SAMPLE_RATE; left;) {
		size_t n = left < 257 ? left : 257;
		render(e, out, NULL, n);
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
	Engine e = {0};
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
		render(&e, warmup, NULL, 4096);
		assert(measure(&e, &crossings) > 1e-6);
		assert(abs(crossings - expected[i]) <= 2);
	}
	const uint8_t off[] = {0x90, 69, 0};
	e.dsp->api->midi_msg_in(e.dsp->instance, e.config.midi, off);
	render(&e, warmup, NULL, 4096);
	assert(measure(&e, &crossings) < 1e-12);
	close_engine(&e);
	puts("OK: synth defaults, note on/off, pitch bend 440/220/880/440 Hz");
}

static void test_synth_blocks(void) {
	const Event events[] = {{0, 26, 300, {0}, 0}, {0, 28, 80, {0}, 0}, // Filter cutoff and contour.
	    {0, 29, 100, {0}, 0}, {0, 30, 200, {0}, 0},                    // Filter attack and decay.
	    {0, 31, 20, {0}, 0}, {0, 32, 80, {0}, 0},                      // Filter sustain and release.
	    {0, -1, 0, {0x90, 60, 100}, 0}, {12345, 26, 800, {0}, 0}, {17001, -1, 0, {0x80, 60, 0}, 0},
	    {18001, -1, 0, {0x90, 64, 100}, 0}, {20001, -1, 0, {0x80, 64, 0}, 0}};
	const size_t blocks[] = {BLOCK, 1, 43, 44, 45, 257}; // Around the 44-sample control period.
	float reference[DEFAULT_SAMPLE_RATE / 2], out[BLOCK];
	for (size_t b = 0; b < sizeof(blocks) / sizeof(*blocks); ++b) {
		Engine e = {.events = events, .count = sizeof(events) / sizeof(*events)};
		assert(!open_bundle(&e, "plugins/synth_mono/build/plugin.perone", DEFAULT_SAMPLE_RATE));
		for (size_t pos = 0; pos < DEFAULT_SAMPLE_RATE / 2;) {
			size_t left = DEFAULT_SAMPLE_RATE / 2 - pos, n = left < blocks[b] ? left : blocks[b];
			render(&e, out, NULL, n);
			for (size_t i = 0; i < n; ++i)
				if (!b)
					reference[pos + i] = out[i];
				else
					assert(out[i] == reference[pos + i]);
			pos += n;
		}
		close_engine(&e);
	}
	puts("OK: synth filter envelope and automation are independent of block size");
}

static void test_effect(void) {
	Engine e = {0};
	assert(!open_bundle(&e, "plugins/tibia_test/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	float in[8193] = {1}, out[8193];
	render(&e, out, in, 8193);
	assert(out[0] > 0 && out[0] < 1);
	for (size_t i = 0; i < 8193; ++i)
		assert(isfinite(out[i]));
	e.dsp->api->set_parameter(e.dsp->instance, 3, 1);
	render(&e, out, in, 8193);
	for (size_t i = 0; i < 8193; ++i)
		assert(out[i] == in[i]);
	e.dsp->api->set_parameter(e.dsp->instance, 1, 1e6f);
	render(&e, out, NULL, 8193);
	close_engine(&e);
	puts("OK: effect memory, audio input, bypass, delay bounds");
}

static void test_brickworks_effect(void) {
	Engine e = {0};
	float in[512] = {1}, out[512];
	assert(!open_bundle(&e, "plugins/fx_svf/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	assert(e.config.input == 1 && e.config.midi == -1);
	assert(!e.dsp->api->get_parameter && !e.dsp->api->midi_msg_in);
	render(&e, out, in, 512);
	double energy = 0;
	for (size_t i = 0; i < 512; ++i) {
		assert(isfinite(out[i]));
		energy += out[i] * out[i];
	}
	assert(energy > 0);
	close_engine(&e);
	puts("OK: unmodified Brickworks effect, audio buses and absent optional functions");
}

static void test_echo_drums(void) {
	Engine echo = {0};
	float input[1025] = {1}, out[1025];
	input[200] = .25f;
	assert(!open_bundle(&echo, "plugins/echo/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	echo.dsp->api->set_parameter(echo.dsp->instance, 0, 1); // 1 ms -> 44 samples.
	echo.dsp->api->set_parameter(echo.dsp->instance, 3, 1);
	echo.dsp->api->set_parameter(echo.dsp->instance, 4, 0);
	echo.dsp->api->set_parameter(echo.dsp->instance, 5, 0);
	echo.dsp->api->set_parameter(echo.dsp->instance, 6, 0);
	const Event change = {100, 0, 2, {0}, 0};
	echo.events = &change;
	echo.count = 1;
	render(&echo, out, input, 1025);
	for (int i = 0; i < 1025; ++i)
		assert(out[i] == (i == 44 ? 1 : i == 288 ? .25f : 0));
	close_engine(&echo);
	Engine a = {0}, b = {0};
	float x[2000], y[2000];
	assert(!open_bundle(&a, "plugins/drums/build/plugin.perone", DEFAULT_SAMPLE_RATE) &&
	    !open_bundle(&b, "plugins/drums/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	const Event hits[] = {{0, -1, 0, {0x90, 4, 127}, 0}, {100, 0, .5f, {0}, 1}, {200, -1, 0, {0x90, 1, 100}, 2},
	    {800, -1, 0, {0x90, 2, 80}, 3}};
	a.events = b.events = hits;
	a.count = b.count = 4;
	render(&a, x, NULL, 2000);
	for (size_t i = 0; i < 2000;) {
		size_t n = 2000 - i < 257 ? 2000 - i : 257;
		render(&b, y + i, NULL, n);
		i += n;
	}
	assert(!memcmp(x, y, sizeof(x)));
	for (int i = 0; i < 100; ++i) {
		const uint8_t hit[] = {0x90, i % 7, 127};
		a.dsp->api->midi_msg_in(a.dsp->instance, a.config.midi, hit);
	}
	a.dsp->api->set_parameter(a.dsp->instance, 0, 0);
	render(&a, x, NULL, 2000);
	for (int i = 0; i < 2000; ++i)
		assert(x[i] == 0 && isfinite(y[i]));
	close_engine(&a);
	close_engine(&b);
	puts("OK: echo timing/automation, deterministic percussion, voice bounds, live drum gain");
}

static void test_shape(void) {
	Engine e = {0};
	float in[32], out[32];
	assert(!open_bundle(&e, "plugins/shape/build/plugin.perone", DEFAULT_SAMPLE_RATE));
	for (int i = 0; i < 32; ++i)
		in[i] = 1;
	e.dsp->api->set_parameter(e.dsp->instance, 2, .5f);
	const Event events[] = {{3, 0, .5f, {0}, 0}, {3, 2, 0, {0}, 1}};
	e.events = events;
	e.count = 2;
	render(&e, out, in, 32);
	for (int i = 0; i < 32; ++i)
		assert(fabsf(out[i] - (i < 3 ? tanhf(1) * powf(.5f, i + 1) : tanhf(.5f))) < 1e-6f);
	close_engine(&e);
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
