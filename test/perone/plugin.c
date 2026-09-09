// Host lifecycle fixture. Built only for tests, independently of Tibia/Brickworks.
#include "perone.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	perone_callbacks callbacks;
	float gain, rate, *memory;
	int initialized;
} Instance;

static int live, initialized;

static int fails(const char *stage) {
	const char *s = getenv("PERONE_TEST_FAIL");
	return s && !strcmp(s, stage);
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
	assert(i->rate == 44100 && i->memory);
	*i->memory = 123;
}

static void set(void *p, size_t index, float value) {
	assert(index == 1);
	((Instance *)p)->gain = value;
}

static float get(void *p, size_t index) {
	assert(index == 0);
	return ((Instance *)p)->gain;
}

static void process(void *p, const float **in, float **out, size_t n) {
	Instance *i = p;
	assert(!in && i->initialized && *i->memory == 123);
	const char *bin = i->callbacks.get_bindir(i->callbacks.handle),
	           *data = i->callbacks.get_datadir(i->callbacks.handle);
	assert(!strncmp(bin, data, strlen(data)) && bin[strlen(data)] == '/');
	for (size_t k = 0; k < n; ++k) {
		out[0][k] = i->gain;
		out[1][k] = -i->gain;
	}
}

__attribute__((visibility("default"))) const perone_api *perone_get_api(uint32_t version) {
	static const perone_api api = {.alloc = allocate,
	    .free = release,
	    .init = init,
	    .fini = fini,
	    .set_sample_rate = rate,
	    .mem_req = mem_req,
	    .mem_set = mem_set,
	    .reset = reset,
	    .process = process,
	    .set_parameter = set,
	    .get_parameter = get};
	static const perone_api missing = {0};
	return version != PERONE_ABI_VERSION || fails("abi") ? NULL : fails("function") ? &missing : &api;
}

__attribute__((destructor)) static void unloaded(void) {
	assert(!live && !initialized);
}
