#include "engine.h"
#include <stdlib.h>

static const char *bindir(void *handle) { return ((Module *)handle)->bindir; }
static const char *datadir(void *handle) { return ((Module *)handle)->datadir; }

void close_engine(Engine *e) {
	if (e->initialized) e->module->api->fini(e->instance);
	free(e->memory);
	if (e->instance) e->module->api->free(e->instance);
	unload_module(e->module);
	*e = (Engine){0};
}

int open_engine(Engine *e, const char *path, const PluginConfig *config) {
	if (!(e->module = load_module(path, config))) return -1;
	const perone_api *a = e->module->api;
	perone_callbacks callbacks = {e->module, bindir, datadir, NULL};
	if (!(e->instance = a->alloc()) || a->init(e->instance, &callbacks)) goto fail;
	e->initialized = 1;
	for (int i = 0; i < config->nparams; ++i)
		if (!(config->outputs & (UINT64_C(1) << i))) a->set_parameter(e->instance, i, config->defaults[i]);
	a->set_sample_rate(e->instance, SAMPLE_RATE);
	size_t size = a->mem_req(e->instance);
	if (size) {
		if (!(e->memory = malloc(size))) goto fail;
		a->mem_set(e->instance, e->memory);
	}
	a->reset(e->instance);
	return 0;
fail:
	close_engine(e);
	return -1;
}

// Split at events and adapt interleaved stereo to the plugin's planar buffers.
void render(Engine *e, float *out, const float *in, size_t frames) {
	static const float silence[BLOCK];
	float input[2][BLOCK], output[2][BLOCK];
	const Module *m = e->module;
	const PluginConfig *c = &m->config;
	while (frames) {
		while (e->next < e->count && e->events[e->next].time <= e->time) {
			const Event *v = &e->events[e->next++];
			if (v->parameter >= 0)
				e->module->api->set_parameter(e->instance, v->parameter, v->value);
			else if (e->module->api->midi_msg_in)
				e->module->api->midi_msg_in(e->instance, c->midi, v->midi);
		}
		size_t n = frames < BLOCK ? frames : BLOCK;
		if (e->next < e->count && e->events[e->next].time - e->time < n)
			n = e->events[e->next].time - e->time;
		const float *x[MAX_INPUTS] = {0}; // Optional disconnected buses receive NULL.
		if (c->input) x[c->input_offset] = in ? in : silence;
		if (c->input == 2) x[c->input_offset + 1] = silence;
		float *y[2] = {out, output[1]};
		if (c->input == 2 && in) {
			for (size_t i = 0; i < n; ++i) { input[0][i] = in[2 * i]; input[1][i] = in[2 * i + 1]; }
			x[c->input_offset] = input[0]; x[c->input_offset + 1] = input[1];
		}
		if (c->output == 2) y[0] = output[0];
		m->api->process(e->instance, c->inputs ? x : NULL, y, n);
		if (c->output == 2)
			for (size_t i = 0; i < n; ++i) { out[2 * i] = y[0][i]; out[2 * i + 1] = y[1][i]; }
		e->time += n;
		frames -= n;
		out += n * c->output;
		if (in) in += n * c->input;
	}
}
