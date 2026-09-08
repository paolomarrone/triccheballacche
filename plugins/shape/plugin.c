// Explicit waveshaper with optional DC blocking and one-pole low-pass.
#include "tibia.h"
#include <math.h>
#include <stdlib.h>
typedef struct { float p[4], dc, low; } Shape;
const tibia_info *tibia_get_info(void) {
	static const tibia_parameter params[] = {
		{"drive", "linear", 0, 32, 1, 0}, {"level", "linear", 0, 4, 1, 0},
		{"dc", "coefficient", 0, 1, 0, 0}, {"lowpass", "coefficient", 0, 1, 1, 0}
	};
	static const tibia_info info = {1, 0, 4, params}; return &info;
}
void *tibia_new(void) { return malloc(sizeof(Shape)); }
void tibia_init(void *p, const tibia_callbacks *c) { (void)c; *(Shape *)p = (Shape){.p = {1, 1, 0, 1}}; }
void tibia_fini(void *p) { (void)p; }
void tibia_set_sample_rate(void *p, float rate) { (void)p; (void)rate; }
size_t tibia_mem_req(void *p) { (void)p; return 0; }
void tibia_mem_set(void *p, void *mem) { (void)p; (void)mem; }
void tibia_reset(void *p) { Shape *s = p; s->dc = s->low = 0; }
void tibia_set_parameter(void *p, size_t i, float value) { if (i < 4) ((Shape *)p)->p[i] = value; }
float tibia_get_parameter(void *p, size_t i) { return i < 4 ? ((Shape *)p)->p[i] : 0; }
void tibia_process(void *p, const float **in, float **out, size_t n) {
	Shape *s = p;
	for (size_t i = 0; i < n; ++i) {
		float x = tanhf(s->p[0] * in[0][i]);
		if (s->p[2]) { s->dc += s->p[2] * (x - s->dc); x -= s->dc; }
		else s->dc = 0;
		s->low += s->p[3] * (x - s->low);
		out[0][i] = s->low * s->p[1];
	}
}
