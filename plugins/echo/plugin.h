// Three explicit taps; dry and wet levels are ordinary automatable parameters.
#include <stdlib.h>
#include <string.h>
typedef struct { float p[7], rate, *line; size_t length, pos; } Echo;

typedef Echo plugin;
static int plugin_init(void *p, const plugin_callbacks *c) { (void)c; *(Echo *)p = (Echo){0}; return 0; }
static void plugin_fini(void *p) { (void)p; }
static void plugin_set_sample_rate(void *p, float rate) { Echo *e = p; e->rate = rate; e->length = (size_t)(2 * rate) + 1; }
static size_t plugin_mem_req(void *p) { return ((Echo *)p)->length * sizeof(float); }
static void plugin_mem_set(void *p, void *mem) { ((Echo *)p)->line = mem; }
static void plugin_reset(void *p) { Echo *e = p; e->pos = 0; memset(e->line, 0, e->length * sizeof(float)); }
static void plugin_set_parameter(void *p, size_t i, float value) { if (i < 7) ((Echo *)p)->p[i] = value; }
static float plugin_get_parameter(void *p, size_t i) { return i < 7 ? ((Echo *)p)->p[i] : 0; }
static void plugin_process(void *p, const float **in, float **out, size_t n) {
	Echo *e = p; size_t delays[3];
	for (int d = 0; d < 3; ++d) delays[d] = (size_t)(e->p[d] * .001f * e->rate + .5f);
	for (size_t i = 0; i < n; ++i) {
		e->line[e->pos] = in[0][i]; float x = in[0][i] * e->p[6];
		for (int d = 0; d < 3; ++d) x += e->p[d + 3] * e->line[(e->pos + e->length - delays[d]) % e->length];
		out[0][i] = x; e->pos = (e->pos + 1) % e->length;
	}
}
