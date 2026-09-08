// Three explicit taps; dry and wet levels are ordinary automatable parameters.
#include "tibia.h"
#include <stdlib.h>
#include <string.h>
typedef struct { float p[7], rate, *line; size_t length, pos; } Echo;
const tibia_info *tibia_get_info(void) {
	static const tibia_parameter params[] = {
		{"time1", "ms", 0, 2000, 109, 0}, {"time2", "ms", 0, 2000, 233, 0}, {"time3", "ms", 0, 2000, 367, 0},
		{"level1", "linear", 0, 1, .3f, 0}, {"level2", "linear", 0, 1, .15f, 0},
		{"level3", "linear", 0, 1, .1f, 0}, {"dry", "linear", 0, 1, 1, 0}
	};
	static const tibia_info info = {1, 0, 7, params}; return &info;
}
void *tibia_new(void) { return malloc(sizeof(Echo)); }
void tibia_init(void *p, const tibia_callbacks *c) { (void)c; *(Echo *)p = (Echo){.p = {109, 233, 367, .3f, .15f, .1f, 1}}; }
void tibia_fini(void *p) { (void)p; }
void tibia_set_sample_rate(void *p, float rate) { Echo *e = p; e->rate = rate; e->length = (size_t)(2 * rate) + 1; }
size_t tibia_mem_req(void *p) { return ((Echo *)p)->length * sizeof(float); }
void tibia_mem_set(void *p, void *mem) { ((Echo *)p)->line = mem; }
void tibia_reset(void *p) { Echo *e = p; e->pos = 0; memset(e->line, 0, e->length * sizeof(float)); }
void tibia_set_parameter(void *p, size_t i, float value) { if (i < 7) ((Echo *)p)->p[i] = value; }
float tibia_get_parameter(void *p, size_t i) { return i < 7 ? ((Echo *)p)->p[i] : 0; }
void tibia_process(void *p, const float **in, float **out, size_t n) {
	Echo *e = p; size_t delays[3];
	for (int d = 0; d < 3; ++d) delays[d] = (size_t)(e->p[d] * .001f * e->rate + .5f);
	for (size_t i = 0; i < n; ++i) {
		e->line[e->pos] = in[0][i]; float x = in[0][i] * e->p[6];
		for (int d = 0; d < 3; ++d) x += e->p[d + 3] * e->line[(e->pos + e->length - delays[d]) % e->length];
		out[0][i] = x; e->pos = (e->pos + 1) % e->length;
	}
}
