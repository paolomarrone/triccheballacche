#include "posix/module.h"
#include "script.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mock_process(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i)
		out[0][i] = in[0][i] + *(float *)p;
}

static void mock_param(void *p, size_t index, float v) {
	(void)index;
	*(float *)p = v;
}

static void mock_midi(void *p, size_t index, const uint8_t *v) {
	assert(index == 7);
	*(float *)p = v[1];
}

static void test_scheduler(void) {
	float value = 0, out[8195] = {123};
	out[8194] = 123;
	const perone_api api = {.process = mock_process, .set_parameter = mock_param, .midi_msg_in = mock_midi};
	DSP dsp = {.api = &api, .instance = &value};
	PluginConfig config = {.input = 1, .inputs = 1, .output = 1, .midi = 7};
	const Event events[] = {
	    {0, 0, 1, {0}, 0}, {4, 0, 2, {0}, 0}, {4, -1, 0, {0x90, 3, 100}, 0}, {8, 0, 4, {0}, 0}, {8192, 0, 5, {0}, 0}};
	Engine e = {.dsp = &dsp, .config = config, .events = events, .count = 5};
	render(&e, out + 1, NULL, 8);
	assert(e.next == 3 && e.time == 8);
	render(&e, out + 9, NULL, 8185);
	for (int i = 0; i < 8193; ++i)
		assert(out[i + 1] == (i < 4 ? 1 : i < 8 ? 3 : i < 8192 ? 4 : 5));
	assert(out[0] == 123 && out[8194] == 123 && e.time == 8193 && e.next == 5);
	puts("OK: sample timing, simultaneous events, callback boundaries, large buffers");
}

static void stereo_process(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		out[0][i] = in[0][i] + *(float *)p;
		out[1][i] = in[1][i] - *(float *)p;
	}
}

static void test_stereo_scheduler(void) {
	enum { FRAMES = 8193 };

	float value = 0, input[FRAMES * 2], out[FRAMES * 2 + 2], split[FRAMES * 2];
	for (int i = 0; i < FRAMES; ++i) {
		input[2 * i] = i;
		input[2 * i + 1] = -2 * i;
	}
	const perone_api api = {.process = stereo_process, .set_parameter = mock_param};
	DSP dsp = {.api = &api, .instance = &value};
	PluginConfig config = {.input = 2, .inputs = 2, .output = 2, .midi = -1};
	const Event events[] = {{0, 0, 1, {0}, 0}, {7, 0, 2, {0}, 0}, {512, 0, 3, {0}, 0}, {8192, 0, 4, {0}, 0}};
	Engine e = {.dsp = &dsp, .config = config, .events = events, .count = 4};
	out[0] = out[FRAMES * 2 + 1] = 123;
	render(&e, out + 1, input, FRAMES);
	assert(out[0] == 123 && out[FRAMES * 2 + 1] == 123);
	e.next = e.time = 0;
	for (size_t i = 0; i < FRAMES;) {
		size_t n = FRAMES - i < 43 ? FRAMES - i : 43;
		render(&e, split + 2 * i, input + 2 * i, n);
		i += n;
	}
	for (int i = 0; i < FRAMES; ++i) {
		float v = i < 7 ? 1 : i < 512 ? 2 : i < 8192 ? 3 : 4;
		assert(out[2 * i + 1] == i + v && out[2 * i + 2] == -2 * i - v);
		assert(split[2 * i] == out[2 * i + 1] && split[2 * i + 1] == out[2 * i + 2]);
	}
	render(&e, split, NULL, 3);
	for (int i = 0; i < 3; ++i)
		assert(split[2 * i] == 4 && split[2 * i + 1] == -4);
	puts("OK: stereo channel separation, event offsets, large buffers, block invariance and silent input");
}

static void disconnected_process(void *p, const float **in, float **out, size_t n) {
	assert(!in[0] && !in[3]);
	stereo_process(p, in + 1, out, n);
}

static void test_disconnected_inputs(void) {
	float value = 1, in[] = {2, 3, 4, 5}, out[4];
	const perone_api api = {.process = disconnected_process};
	DSP dsp = {.api = &api, .instance = &value};
	PluginConfig config = {.input = 2, .output = 2, .inputs = 4, .input_offset = 1, .midi = -1};
	Engine e = {.dsp = &dsp, .config = config};
	render(&e, out, in, 2);
	assert(out[0] == 3 && out[1] == 2 && out[2] == 5 && out[3] == 4);
	render(&e, out, NULL, 2);
	assert(out[0] == 1 && out[1] == -1);
	puts("OK: disconnected optional inputs preserve flattened bus positions");
}

static void test_lifecycle(void) {
	const char *path = "build/fixture.perone";
	const char *stages[] = {"alloc", "init", "memory", "abi", "function"};
	for (size_t i = 0; i < sizeof(stages) / sizeof(*stages); ++i) {
		assert(!setenv("PERONE_TEST_FAIL", stages[i], 1));
		Engine e = {0};
		assert(open_bundle(&e, path, DEFAULT_SAMPLE_RATE) < 0);
		assert(!e.dsp);
		close_engine(&e);
	}
	assert(!unsetenv("PERONE_TEST_FAIL"));
	Engine e = {0};
	assert(!open_bundle(&e, path, DEFAULT_SAMPLE_RATE));
	DSP *original = e.dsp;
	assert(open_bundle(&e, path, DEFAULT_SAMPLE_RATE) < 0 && e.dsp == original);
	assert(e.config.nparams == 3 && e.config.outputs == 1);
	float out[6];
	render(&e, out, NULL, 3);
	assert(out[0] == .5f && out[1] == -.5f);
	assert(e.dsp->api->get_parameter(e.dsp->instance, 0) == .5f);
	close_engine(&e);
	puts("OK: Perone allocation/init/memory/ABI failures, cleanup, callbacks and output-first defaults");
}

static void test_bundle(const char *path) {
	Engine e = {0};
	assert(!open_bundle(&e, path, DEFAULT_SAMPLE_RATE));
	const PluginConfig *c = &e.config;
	float in[BLOCK * 2], out[BLOCK * 2];
	const Event notes[] = {{0, -1, 0, {0x90, 60, 100}, 0}, {4097, -1, 0, {0x80, 60, 0}, 1}};
	if (c->midi >= 0) {
		e.events = notes;
		e.count = 2;
	}
	double energy = 0;
	for (int pos = 0; pos < 8193;) {
		int n = 8193 - pos < BLOCK ? 8193 - pos : BLOCK;
		for (int i = 0; i < n; ++i)
			for (int ch = 0; ch < c->input; ++ch)
				in[i * c->input + ch] = .2f * sinf((pos + i) * (ch ? .09f : .06f));
		render(&e, out, c->input ? in : NULL, n);
		for (int i = 0; i < n * c->output; ++i) {
			assert(isfinite(out[i]));
			energy += out[i] * out[i];
		}
		pos += n;
	}
	assert(energy > 0 && e.time == 8193);
	printf("OK: %s (%d -> %d channels)\n", path, c->input, c->output);
	close_engine(&e);
}

int main(int argc, char **argv) {
	if (argc > 1) {
		for (int i = 1; i < argc; ++i)
			test_bundle(argv[i]);
		return 0;
	}
	test_scheduler();
	test_stereo_scheduler();
	test_disconnected_inputs();
	test_lifecycle();
	Engine missing = {0};
	assert(open_bundle(&missing, "build/nonexistent.so", DEFAULT_SAMPLE_RATE) != 0);
	close_engine(&missing);
	puts("All tests passed.");
	return 0;
}
