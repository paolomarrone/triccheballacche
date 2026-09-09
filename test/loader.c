#include "script.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mock_process(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i) out[0][i] = in[0][i] + *(float *)p;
}
static void mock_param(void *p, size_t index, float v) { (void)index; *(float *)p = v; }
static void mock_midi(void *p, size_t index, const uint8_t *v) { assert(index == 7); *(float *)p = v[1]; }

static void test_scheduler(void) {
	float value = 0, out[8195] = {123};
	out[8194] = 123;
	const perone_api api = {.process = mock_process, .set_parameter = mock_param, .midi_msg_in = mock_midi};
	Module module = {.api = &api, .config = {.input = 1, .inputs = 1, .output = 1, .midi = 7}};
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

static void stereo_process(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i) { out[0][i] = in[0][i] + *(float *)p; out[1][i] = in[1][i] - *(float *)p; }
}
static void test_stereo_scheduler(void) {
	enum { FRAMES = 8193 };
	float value = 0, input[FRAMES * 2], out[FRAMES * 2 + 2], split[FRAMES * 2];
	for (int i = 0; i < FRAMES; ++i) { input[2 * i] = i; input[2 * i + 1] = -2 * i; }
	const perone_api api = {.process = stereo_process, .set_parameter = mock_param};
	Module module = {.api = &api, .config = {.input = 2, .inputs = 2, .output = 2, .midi = -1}};
	const Event events[] = {{0, 0, 1, {0}, 0}, {7, 0, 2, {0}, 0}, {512, 0, 3, {0}, 0}, {8192, 0, 4, {0}, 0}};
	Engine e = {.module = &module, .instance = &value, .events = events, .count = 4};
	out[0] = out[FRAMES * 2 + 1] = 123;
	render(&e, out + 1, input, FRAMES);
	assert(out[0] == 123 && out[FRAMES * 2 + 1] == 123);
	e.next = e.time = 0;
	for (size_t i = 0; i < FRAMES;) {
		size_t n = FRAMES - i < 43 ? FRAMES - i : 43;
		render(&e, split + 2 * i, input + 2 * i, n); i += n;
	}
	for (int i = 0; i < FRAMES; ++i) {
		float v = i < 7 ? 1 : i < 512 ? 2 : i < 8192 ? 3 : 4;
		assert(out[2 * i + 1] == i + v && out[2 * i + 2] == -2 * i - v);
		assert(split[2 * i] == out[2 * i + 1] && split[2 * i + 1] == out[2 * i + 2]);
	}
	render(&e, split, NULL, 3);
	for (int i = 0; i < 3; ++i) assert(split[2 * i] == 4 && split[2 * i + 1] == -4);
	puts("OK: stereo channel separation, event offsets, large buffers, block invariance and silent input");
}

static void disconnected_process(void *p, const float **in, float **out, size_t n) {
	assert(!in[0] && !in[3]);
	stereo_process(p, in + 1, out, n);
}
static void test_disconnected_inputs(void) {
	float value = 1, in[] = {2, 3, 4, 5}, out[4];
	const perone_api api = {.process = disconnected_process};
	Module module = {.api = &api, .config = {.input = 2, .output = 2, .inputs = 4, .input_offset = 1, .midi = -1}};
	Engine e = {.module = &module, .instance = &value};
	render(&e, out, in, 2);
	assert(out[0] == 3 && out[1] == 2 && out[2] == 5 && out[3] == 4);
	render(&e, out, NULL, 2);
	assert(out[0] == 1 && out[1] == -1);
	puts("OK: disconnected optional inputs preserve flattened bus positions");
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
	assert(!open_bundle(&e, "plugins/synth_mono/build/plugin.perone"));
	assert(e.module->config.midi == 0 && !e.module->config.input);
	assert(e.module->config.nparams == 39);
	assert(e.module->config.outputs & (UINT64_C(1) << 38));
	float warmup[4096];
	int crossings;
	assert(measure(&e, &crossings) < 1e-12);
	const uint8_t note[] = {0x90, 69, 100};
	e.module->api->midi_msg_in(e.instance, e.module->config.midi, note);
	const uint8_t bends[][3] = {{0xe0, 0, 64}, {0xe0, 0, 0}, {0xe0, 127, 127}, {0xe0, 0, 64}};
	const int expected[] = {440, 220, 880, 440};
	for (size_t i = 0; i < 4; ++i) {
		e.module->api->midi_msg_in(e.instance, e.module->config.midi, bends[i]);
		render(&e, warmup, NULL, 4096);
		assert(measure(&e, &crossings) > 1e-6);
		assert(abs(crossings - expected[i]) <= 2);
	}
	const uint8_t off[] = {0x90, 69, 0};
	e.module->api->midi_msg_in(e.instance, e.module->config.midi, off);
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
		assert(!open_bundle(&e, "plugins/synth_mono/build/plugin.perone"));
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
	assert(!open_bundle(&e, "plugins/tibia_test/build/plugin.perone"));
	float in[8193] = {1}, out[8193];
	render(&e, out, in, 8193);
	assert(out[0] > 0 && out[0] < 1);
	for (size_t i = 0; i < 8193; ++i) assert(isfinite(out[i]));
	e.module->api->set_parameter(e.instance, 3, 1);
	render(&e, out, in, 8193);
	for (size_t i = 0; i < 8193; ++i) assert(out[i] == in[i]);
	e.module->api->set_parameter(e.instance, 1, 1e6f);
	render(&e, out, NULL, 8193);
	close_engine(&e);
	puts("OK: effect memory, audio input, bypass, delay bounds");
}

static void test_brickworks_effect(void) {
	Engine e = {0}; float in[512] = {1}, out[512];
	assert(!open_bundle(&e, "plugins/fx_svf/build/plugin.perone"));
	assert(e.module->config.input == 1 && e.module->config.midi == -1);
	assert(!e.module->api->get_parameter && !e.module->api->midi_msg_in);
	render(&e, out, in, 512);
	double energy = 0;
	for (size_t i = 0; i < 512; ++i) { assert(isfinite(out[i])); energy += out[i] * out[i]; }
	assert(energy > 0);
	close_engine(&e);
	puts("OK: unmodified Brickworks effect, audio buses and absent optional functions");
}

static void test_lifecycle(void) {
	const char *path = "build/fixture.perone";
	const char *stages[] = {"alloc", "init", "memory", "abi", "function"};
	for (size_t i = 0; i < sizeof(stages) / sizeof(*stages); ++i) {
		assert(!setenv("PERONE_TEST_FAIL", stages[i], 1));
		Engine e = {0}; assert(open_bundle(&e, path) < 0);
		assert(!e.module && !e.instance && !e.memory); close_engine(&e);
	}
	assert(!unsetenv("PERONE_TEST_FAIL"));
	Engine e = {0}; assert(!open_bundle(&e, path));
	assert(e.module->config.nparams == 2 && e.module->config.outputs == 1);
	float out[6]; render(&e, out, NULL, 3);
	assert(out[0] == .5f && out[1] == -.5f);
	assert(e.module->api->get_parameter(e.instance, 0) == .5f);
	close_engine(&e);
	puts("OK: Perone allocation/init/memory/ABI failures, cleanup, callbacks and output-first defaults");
}

static void test_bundle(const char *path) {
	Engine e = {0};
	assert(!open_bundle(&e, path));
	const PluginConfig *c = &e.module->config;
	float in[BLOCK * 2], out[BLOCK * 2];
	const Event notes[] = {{0, -1, 0, {0x90, 60, 100}, 0}, {4097, -1, 0, {0x80, 60, 0}, 1}};
	if (c->midi >= 0) { e.events = notes; e.count = 2; }
	double energy = 0;
	for (int pos = 0; pos < 8193;) {
		int n = 8193 - pos < BLOCK ? 8193 - pos : BLOCK;
		for (int i = 0; i < n; ++i) for (int ch = 0; ch < c->input; ++ch)
			in[i * c->input + ch] = .2f * sinf((pos + i) * (ch ? .09f : .06f));
		render(&e, out, c->input ? in : NULL, n);
		for (int i = 0; i < n * c->output; ++i) { assert(isfinite(out[i])); energy += out[i] * out[i]; }
		pos += n;
	}
	assert(energy > 0 && e.time == 8193);
	printf("OK: %s (%d -> %d channels)\n", path, c->input, c->output);
	close_engine(&e);
}

int main(int argc, char **argv) {
	if (argc > 1) { for (int i = 1; i < argc; ++i) test_bundle(argv[i]); return 0; }
	test_scheduler();
	test_stereo_scheduler();
	test_disconnected_inputs();
	test_synth();
	test_synth_blocks();
	test_effect();
	test_lifecycle();
	test_brickworks_effect();
	Engine missing = {0};
	assert(open_bundle(&missing, "build/nonexistent.so") != 0);
	close_engine(&missing);
	puts("All tests passed.");
	return 0;
}
