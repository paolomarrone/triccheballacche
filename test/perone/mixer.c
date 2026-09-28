// Small arithmetic DSPs exercise routing through the same ABI as production plugins.
#include "perone.h"
#include <stdlib.h>

typedef struct {
	float value;
	int kind;
} Instance;
static void *allocate(void) {
	return calloc(1, sizeof(Instance));
}
static int init(void *p, const perone_callbacks *callbacks) {
	(void)p;
	(void)callbacks;
	return 0;
}
static void noop(void *p) {
	(void)p;
}
static size_t mem_req(void *p) {
	(void)p;
	return 0;
}
static void mem_set(void *p, void *memory) {
	(void)p;
	(void)memory;
}
static void rate(void *p, float value) {
	(void)p;
	(void)value;
}
static void set(void *p, size_t index, float value) {
	Instance *i = p;
	if (index)
		i->kind = (int)value;
	else
		i->value = value;
}
static void process(void *p, const float **in, float **out, size_t n) {
	Instance *instance = p;
	float v = instance->value;
	for (size_t i = 0; i < n; ++i) {
		switch (instance->kind) {
		case 0:
			out[0][i] = v;
			break;
		case 1:
			out[0][i] = in[0][i] * v;
			break;
		case 2:
			out[0][i] = in[0][i] + v;
			break;
		case 3:
			out[0][i] = v;
			out[1][i] = -.5f * v;
			break;
		case 4:
			out[0][i] = in[1][i] * v;
			out[1][i] = in[0][i] * v;
			break;
		case 5:
			out[0][i] = in[0][i] * v;
			out[1][i] = -out[0][i];
			break;
		case 6:
			out[0][i] = .5f * (in[0][i] + in[1][i]) * v;
			break;
		}
	}
}
__attribute__((visibility("default"))) const perone_api *perone_get_api(uint32_t version) {
	static const perone_api api = {.alloc = allocate,
	    .free = free,
	    .init = init,
	    .fini = noop,
	    .set_sample_rate = rate,
	    .mem_req = mem_req,
	    .mem_set = mem_set,
	    .reset = noop,
	    .set_parameter = set,
	    .process = process};
	return version == PERONE_ABI_VERSION ? &api : NULL;
}
