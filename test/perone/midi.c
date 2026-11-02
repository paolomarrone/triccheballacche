// MIDI-only ABI 3 fixture. No Tibia checkout is needed to test the host.
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
	int transpose, delay, fault;
	size_t count;
	struct {
		uint8_t data[3];
		size_t remaining;
	} pending[256];
} Instance;
static void *allocate(void) {
	return calloc(1, sizeof(Instance));
}
static int init(void *p, const perone_callbacks *cb) {
	((Instance *)p)->callbacks = *cb;
	return cb->midi_msg_out ? 0 : -1;
}
static void fini(void *p) {
	(void)p;
}
static void rate(void *p, float value) {
	(void)p;
	assert(value > 0);
}
static size_t mem_req(void *p) {
	(void)p;
	return 0;
}
static void mem_set(void *p, void *mem) {
	(void)p;
	assert(!mem);
}
static void reset(void *p) {
	((Instance *)p)->count = 0;
}
static void set(void *p, size_t index, float value) {
	Instance *i = p;
	assert(index < 3);
	if (!index)
		i->transpose = value;
	else if (index == 1)
		i->delay = value;
	else
		i->fault = value;
}
static void midi(void *p, size_t bus, const uint8_t *data) {
	Instance *i = p;
	assert(bus == 1 && i->count < 256);
	size_t n = i->count++;
	memcpy(i->pending[n].data, data, 3);
	i->pending[n].remaining = i->delay;
	if ((data[0] & 0xe0) == 0x80)
		i->pending[n].data[1] += i->transpose;
}
static void process(void *p, const float **in, float **out, size_t frames) {
	Instance *i = p;
	assert(!in && !out);
	if (!frames)
		return;
	if (i->fault) {
		uint8_t data[3] = {0x90, i->fault == 3 ? 128 : 60, 100};
		for (unsigned n = 0; n < (i->fault == 4 ? 257u : 1u); ++n)
			i->callbacks.midi_msg_out(i->callbacks.handle, i->fault == 1 ? 1 : 0, data, i->fault == 2 ? frames : 0);
		return;
	}
	size_t kept = 0;
	for (size_t n = 0; n < i->count; ++n) {
		if (i->pending[n].remaining < frames) {
			uint8_t data[3];
			memcpy(data, i->pending[n].data, 3);
			i->callbacks.midi_msg_out(i->callbacks.handle, 0, data, i->pending[n].remaining);
			memset(data, 0, 3); // The host must copy borrowed bytes immediately.
		} else {
			i->pending[n].remaining -= frames;
			i->pending[kept++] = i->pending[n];
		}
	}
	i->count = kept;
}
__attribute__((visibility("default"))) const perone_api *perone_get_api(uint32_t version) {
	static const perone_api api = {.alloc = allocate,
	    .free = free,
	    .init = init,
	    .fini = fini,
	    .set_sample_rate = rate,
	    .mem_req = mem_req,
	    .mem_set = mem_set,
	    .reset = reset,
	    .process = process,
	    .set_parameter = set,
	    .midi_msg_in = midi};
	return version == 3 ? &api : NULL;
}
