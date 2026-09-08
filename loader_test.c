#include "engine.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void mock_process(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i) out[0][i] = in[0][i] + *(float *)p;
}
static void mock_param(void *p, size_t index, float v) { (void)index; *(float *)p = v; }
static void mock_midi(void *p, size_t index, const uint8_t *v) { (void)index; *(float *)p = v[1]; }

static void test_scheduler(void) {
	float value = 0, out[8195] = {123};
	out[8194] = 123;
	TibiaModule module = {.process = mock_process, .set_parameter = mock_param, .midi_msg_in = mock_midi};
	const Event events[] = {
		{0, 0, 1, {0}, 0}, {4, 0, 2, {0}, 0}, {4, -1, 0, {0x90, 3, 100}, 0},
		{8, 0, 4, {0}, 0}, {8192, 0, 5, {0}, 0}
	};
	Engine e = {.module = &module, .instance = &value, .events = events, .count = 5};
	render(&e, out + 1, NULL, 8);
	assert(e.next == 3 && e.time == 8);
	render(&e, out + 9, NULL, 8185);
	for (int i = 0; i < 8193; ++i)
		assert(out[i + 1] == (i < 4 ? 1 : i < 8 ? 3 : i < 8192 ? 4 : 5));
	assert(out[0] == 123 && out[8194] == 123 && e.time == 8193 && e.next == 5);
	puts("OK: sample timing, simultaneous events, callback boundaries, large buffers");
}

static double measure(Engine *e, int *crossings) {
	float out[257], previous = 0;
	double energy = 0;
	*crossings = 0;
	for (size_t left = SAMPLE_RATE; left;) {
		size_t n = left < 257 ? left : 257;
		render(e, out, NULL, n);
		for (size_t i = 0; i < n; ++i) {
			assert(isfinite(out[i]));
			energy += out[i] * out[i];
			if (previous < 0 && out[i] >= 0) ++*crossings;
			previous = out[i];
		}
		left -= n;
	}
	return energy / SAMPLE_RATE;
}

static void test_synth(void) {
	Engine e = {0};
	assert(!open_engine(&e, "examples/synth_mono/plugin.so"));
	float warmup[4096];
	int crossings;
	assert(measure(&e, &crossings) < 1e-12);
	const uint8_t note[] = {0x90, 69, 100};
	e.module->midi_msg_in(e.instance, 0, note);
	const uint8_t bends[][3] = {{0xe0, 0, 64}, {0xe0, 0, 0}, {0xe0, 127, 127}, {0xe0, 0, 64}};
	const int expected[] = {440, 220, 880, 440};
	for (size_t i = 0; i < 4; ++i) {
		e.module->midi_msg_in(e.instance, 0, bends[i]);
		render(&e, warmup, NULL, 4096);
		assert(measure(&e, &crossings) > 1e-6);
		assert(abs(crossings - expected[i]) <= 2);
	}
	const uint8_t off[] = {0x90, 69, 0};
	e.module->midi_msg_in(e.instance, 0, off);
	render(&e, warmup, NULL, 4096);
	assert(measure(&e, &crossings) < 1e-12);
	close_engine(&e);
	puts("OK: synth defaults, note on/off, pitch bend 440/220/880/440 Hz");
}

static void test_synth_blocks(void) {
	const Event events[] = {
		{0, 26, 300, {0}, 0}, {0, 28, 80, {0}, 0}, // Filter cutoff and contour.
		{0, 29, 100, {0}, 0}, {0, 30, 200, {0}, 0}, // Filter attack and decay.
		{0, 31, 20, {0}, 0}, {0, 32, 80, {0}, 0}, // Filter sustain and release.
		{0, -1, 0, {0x90, 60, 100}, 0}, {12345, 26, 800, {0}, 0},
		{17001, -1, 0, {0x80, 60, 0}, 0}, {18001, -1, 0, {0x90, 64, 100}, 0},
		{20001, -1, 0, {0x80, 64, 0}, 0}
	};
	const size_t blocks[] = {BLOCK, 1, 43, 44, 45, 257}; // Around the 44-sample control period.
	float reference[SAMPLE_RATE / 2], out[BLOCK];
	for (size_t b = 0; b < sizeof(blocks) / sizeof(*blocks); ++b) {
		Engine e = {.events = events, .count = sizeof(events) / sizeof(*events)};
		assert(!open_engine(&e, "examples/synth_mono/plugin.so"));
		for (size_t pos = 0; pos < SAMPLE_RATE / 2;) {
			size_t left = SAMPLE_RATE / 2 - pos, n = left < blocks[b] ? left : blocks[b];
			render(&e, out, NULL, n);
			for (size_t i = 0; i < n; ++i)
				if (!b) reference[pos + i] = out[i];
				else assert(out[i] == reference[pos + i]);
			pos += n;
		}
		close_engine(&e);
	}
	puts("OK: synth filter envelope and automation are independent of block size");
}

static void test_effect(void) {
	Engine e = {0};
	assert(!open_engine(&e, "examples/tibia_test/plugin.so"));
	float in[8193] = {1}, out[8193];
	render(&e, out, in, 8193);
	assert(out[0] > 0 && out[0] < 1);
	for (size_t i = 0; i < 8193; ++i) assert(isfinite(out[i]));
	e.module->set_parameter(e.instance, 3, 1);
	render(&e, out, in, 8193);
	for (size_t i = 0; i < 8193; ++i) assert(out[i] == in[i]);
	e.module->set_parameter(e.instance, 1, 1e6f);
	render(&e, out, NULL, 8193);
	close_engine(&e);
	puts("OK: effect memory, audio input, bypass, delay bounds");
}

int main(void) {
	test_scheduler();
	test_synth();
	test_synth_blocks();
	test_effect();
	Engine missing = {0};
	assert(open_engine(&missing, "build/nonexistent.so") != 0);
	close_engine(&missing);
	puts("All tests passed.");
	return 0;
}
