#include "posix/module.h"
#include "script.h"
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *produce(void *arg) {
	Messages *q = arg;
	for (unsigned i = 0; i < 100000; ++i)
		while (message_push(q, sizeof(i), &i))
			sched_yield();
	return NULL;
}

static void test_messages(void) {
	Messages q = {.limit = sizeof(unsigned)};
	q.data = malloc(MESSAGE_SLOTS * q.limit);
	assert(q.data);
	unsigned value;
	size_t size;
	assert(!message_pop(&q, &size, &value));
	assert(message_push(&q, q.limit + 1, &value));
	for (unsigned i = 0; i < MESSAGE_SLOTS; ++i)
		assert(!message_push(&q, sizeof(i), &i));
	assert(message_push(&q, 0, NULL));
	for (unsigned i = 0; i < MESSAGE_SLOTS; ++i) {
		assert(message_pop(&q, &size, &value));
		assert(size == sizeof(value) && value == i);
	}
	assert(!message_push(&q, 0, NULL));
	assert(message_pop(&q, &size, &value) && size == 0);
	// Cross both the ring boundary and unsigned counter wrap with a concurrent producer.
	atomic_store(&q.read, UINT_MAX - 20);
	atomic_store(&q.write, UINT_MAX - 20);
	pthread_t thread;
	assert(!pthread_create(&thread, NULL, produce, &q));
	for (unsigned i = 0; i < 100000; ++i) {
		while (!message_pop(&q, &size, &value))
			sched_yield();
		assert(size == sizeof(value) && value == i);
	}
	assert(!pthread_join(thread, NULL));
	free(q.data);
	puts("OK: message bounds, full/empty queues, zero-length messages, FIFO and concurrent counter wrap");
}

typedef struct {
	float values[2], at_message[2];
	unsigned indices[8], messages[2], parameters, count;
	atomic_int *gate;
} Controls;

static void control_parameter(void *p, size_t index, float value) {
	Controls *c = p;
	c->indices[c->parameters++] = index;
	c->values[index] = value;
	if (c->gate) {
		atomic_store(c->gate, 1);
		while (atomic_load(c->gate) == 1)
			sched_yield();
	}
}

static void control_message(void *p, size_t size, const void *data) {
	Controls *c = p;
	assert(size == sizeof(unsigned) && c->count < 2);
	memcpy(c->messages + c->count, data, size);
	c->at_message[c->count++] = c->values[0];
}

static void *apply_control(void *arg) {
	sync_dsp(arg, NULL);
	return NULL;
}

static void test_controls(void) {
	const perone_api api = {.set_parameter = control_parameter, .msg_in = control_message};
	Controls left = {0}, right = {0};
	unsigned data[MESSAGE_SLOTS], first = 7, second = 8;
	DSP a = {.api = &api,
	    .instance = &left,
	    .config = {.nparams = 2},
	    .to_dsp = {.limit = sizeof(unsigned), .data = (unsigned char *)data}};
	DSP b = {.api = &api, .instance = &right};
	watch_dsp(&a, 1);
	// Messages are FIFO, but all pending parameter values precede them, regardless of submission order.
	assert(!send_dsp(&a, sizeof(first), &first));
	edit_dsp(&a, 1, 30);
	edit_dsp(&a, 0, 1);
	edit_dsp(&a, 0, 2);
	assert(!send_dsp(&a, sizeof(second), &second));
	edit_dsp(&a, 0, 3);
	float value;
	assert(!left.parameters && !left.count && !read_dsp(&a, 0, &value));
	sync_dsp(&a, &b);
	assert(read_dsp(&a, 0, &value) && value == 3);
	for (int i = 0; i < 2; ++i) {
		Controls *c = i ? &right : &left;
		assert(c->parameters == 2 && c->indices[0] == 0 && c->indices[1] == 1);
		assert(c->values[0] == 3 && c->values[1] == 30 && c->count == 2);
		assert(c->messages[0] == first && c->messages[1] == second);
		assert(c->at_message[0] == 3 && c->at_message[1] == 3);
	}
	// Pause the audio thread inside the setter: no stale acknowledgement, and a newer edit survives.
	atomic_int gate = 0;
	left.gate = &gate;
	atomic_store(&a.requested[0], UINT_MAX);
	atomic_store(&a.applied[0], UINT_MAX);
	edit_dsp(&a, 0, 4);
	pthread_t thread;
	assert(!pthread_create(&thread, NULL, apply_control, &a));
	while (!atomic_load(&gate))
		sched_yield();
	assert(!read_dsp(&a, 0, &value));
	edit_dsp(&a, 0, 5);
	atomic_store(&gate, 2);
	assert(!pthread_join(thread, NULL));
	assert(left.values[0] == 4 && !read_dsp(&a, 0, &value));
	left.gate = NULL;
	sync_dsp(&a, &b);
	assert(read_dsp(&a, 0, &value) && value == 5 && right.values[0] == 5);
	unsigned applied = left.parameters;
	sync_dsp(&a, &b);
	assert(left.parameters == applied);
	watch_dsp(&a, 0);
	puts("OK: latest parameter values, message order, stereo edits, concurrent acknowledgement and counter wrap");
}

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
	test_messages();
	test_controls();
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
