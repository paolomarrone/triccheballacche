// Explicit waveshaper with optional DC blocking and one-pole low-pass.
#include <math.h>
#include <stdlib.h>
typedef struct { float p[4], dc, low; } Shape;

typedef Shape plugin;
static int plugin_init(void *p, const plugin_callbacks *c) { (void)c; *(Shape *)p = (Shape){0}; return 0; }
static void plugin_fini(void *p) { (void)p; }
static void plugin_set_sample_rate(void *p, float rate) { (void)p; (void)rate; }
static size_t plugin_mem_req(void *p) { (void)p; return 0; }
static void plugin_mem_set(void *p, void *mem) { (void)p; (void)mem; }
static void plugin_reset(void *p) { Shape *s = p; s->dc = s->low = 0; }
static void plugin_set_parameter(void *p, size_t i, float value) { if (i < 4) ((Shape *)p)->p[i] = value; }
static float plugin_get_parameter(void *p, size_t i) { return i < 4 ? ((Shape *)p)->p[i] : 0; }
static void plugin_process(void *p, const float **in, float **out, size_t n) {
	Shape *s = p;
	for (size_t i = 0; i < n; ++i) {
		float x = tanhf(s->p[0] * in[0][i]);
		if (s->p[2]) { s->dc += s->p[2] * (x - s->dc); x -= s->dc; }
		else s->dc = 0;
		s->low += s->p[3] * (x - s->low);
		out[0][i] = s->low * s->p[1];
	}
}
