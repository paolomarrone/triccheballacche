// Host lifecycle fixture. Built only for tests, independently of Tibia/Brickworks.
#include "perone.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef __wasm__
#undef assert
#define assert(x) ((x) ? (void)0 : __builtin_trap())
#endif

typedef struct {
	perone_callbacks callbacks;
	float gain, control, rate, gate, state, *memory;
	int initialized;
	unsigned char message[16];
	size_t message_size;
	int pending_message;
} Instance;

static int live, initialized;

static int fails(const char *stage) {
#ifdef __wasm__
	(void)stage;
	return 0;
#else
	const char *s = getenv("PERONE_TEST_FAIL");
	return s && !strcmp(s, stage);
#endif
}

static void *allocate(void) {
	if (fails("alloc"))
		return NULL;
	void *p = calloc(1, sizeof(Instance));
	if (p)
		++live;
	return p;
}

static void release(void *p) {
	assert(p && !((Instance *)p)->initialized);
	--live;
	free(p);
}

static int init(void *p, const perone_callbacks *callbacks) {
	if (fails("init"))
		return -7;
	Instance *i = p;
	i->callbacks = *callbacks;
	i->initialized = 1;
	++initialized;
	return 0;
}

static void fini(void *p) {
	Instance *i = p;
	assert(i->initialized);
	if (i->memory)
		assert(*i->memory == 123); // Host memory still exists during fini.
	i->initialized = 0;
	--initialized;
}

static void rate(void *p, float value) {
	((Instance *)p)->rate = value;
}

static size_t mem_req(void *p) {
	(void)p;
	return fails("memory") ? SIZE_MAX : sizeof(float);
}

static void mem_set(void *p, void *mem) {
	((Instance *)p)->memory = mem;
}

static void reset(void *p) {
	Instance *i = p;
	assert((i->rate > 0) && i->memory);
	*i->memory = 123;
	i->state = 0;
	i->gate = 1;
}

static void set(void *p, size_t index, float value) {
	assert(index == 1 || index == 2);
	Instance *i = p;
	if (index == 1)
		i->gain = value;
	else
		i->control = value;
}

static float get(void *p, size_t index) {
	assert(index == 0);
	return ((Instance *)p)->gain;
}

static void process(void *p, const float **in, float **out, size_t n) {
	Instance *i = p;
	assert(i->initialized && *i->memory == 123);
	if (i->pending_message) {
		i->callbacks.msg_write(i->callbacks.handle, i->message_size, i->message);
		i->pending_message = 0;
	}
	const char *bin = i->callbacks.get_bindir(i->callbacks.handle),
	           *data = i->callbacks.get_datadir(i->callbacks.handle);
	assert(!strncmp(bin, data, strlen(data)) && bin[strlen(data)] == '/');
	for (size_t k = 0; k < n; ++k) {
#ifdef PERONE_TEST_EFFECT
		assert(in && in[0]);
		if (i->control)
			i->state += i->control * (in[0][k] - i->state);
		else
			i->state = 0;
		out[0][k] = (in[0][k] - i->state) * i->gain;
#else
		assert(!in);
		out[0][k] = i->gain * i->gate * (i->control == 3 ? i->rate / 44100 : 1);
		out[1][k] = -out[0][k];
#endif
	}
}

static void message(void *p, size_t size, const void *data) {
	Instance *i = p;
	assert(size <= sizeof(i->message));
	memcpy(i->message, data, size);
	i->message_size = size;
	i->pending_message = 1;
}

#ifndef PERONE_TEST_EFFECT
static void midi(void *p, size_t bus, const uint8_t *data) {
	assert(bus == 0);
	((Instance *)p)->gate = data[0] == 0x90 && data[2] != 0;
}
#endif

__attribute__((visibility("default"))) const perone_api *perone_get_api(uint32_t version) {
	static const perone_api api = {
	    .alloc = allocate,
	    .free = release,
	    .init = init,
	    .fini = fini,
	    .set_sample_rate = rate,
	    .mem_req = mem_req,
	    .mem_set = mem_set,
	    .reset = reset,
	    .process = process,
	    .set_parameter = set,
	    .get_parameter = get,
	    .msg_in = message,
#ifndef PERONE_TEST_EFFECT
	    .midi_msg_in = midi,
#endif
	};
	static const perone_api missing = {0};
	return version != PERONE_ABI_VERSION || fails("abi") ? NULL : fails("function") ? &missing : &api;
}

__attribute__((destructor)) static void unloaded(void) {
	assert(!live && !initialized);
}
