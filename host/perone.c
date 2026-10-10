#include "dsp_internal.h"
#include "perone.h"
#include "platform.h"
#include <stdlib.h>

typedef struct {
	void *library;
	char *bindir, *datadir;
} PeroneModule;

static void close_module(void *data) {
	PeroneModule *m = data;
	if (!m)
		return;
	library_close(m->library);
	free(m->bindir);
	free(m->datadir);
	free(m);
}
static void *open_module(const char *path) {
	PeroneModule *m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;
	m->bindir = host_parent(path);
	m->datadir = m->bindir ? host_parent(m->bindir) : NULL;
	if (!m->datadir || !(m->library = library_open(path))) {
		close_module(m);
		return NULL;
	}
	return m;
}
const ModuleOps perone_module_ops = {open_module, close_module};
const char *perone_bindir(Module *module) {
	return ((PeroneModule *)module_data(module))->bindir;
}
const char *perone_datadir(Module *module) {
	return ((PeroneModule *)module_data(module))->datadir;
}

typedef struct {
	const perone_api *api;
	void *instance, *memory;
	int initialized;
} Perone;

static const char *bindir(void *handle) {
	return perone_bindir(((DSP *)handle)->module);
}
static const char *datadir(void *handle) {
	return perone_datadir(((DSP *)handle)->module);
}

static void close_instance(DSP *dsp) {
	Perone *p = dsp->instance;
	if (!p)
		return;
	if (p->initialized)
		p->api->fini(p->instance);
	free(p->memory);
	if (p->instance)
		p->api->free(p->instance);
	free(p);
}

static void set(DSP *dsp, size_t index, double value) {
	Perone *p = dsp->instance;
	p->api->set_parameter(p->instance, index, value);
}

static void sync(DSP *dsp) {
	Perone *p = dsp->instance;
	if (atomic_load(&dsp->viewing))
		for (int i = 0; i < dsp->config.nparams; ++i)
			if (dsp->config.parameters[i].flags & PARAM_OUTPUT)
				atomic_store(&dsp->values[i], p->api->get_parameter(p->instance, i));
}

static void reset(DSP *dsp) {
	Perone *p = dsp->instance;
	p->api->reset(p->instance);
	sync(dsp);
}

static void midi(DSP *dsp, size_t bus, const uint8_t *message) {
	Perone *p = dsp->instance;
	p->api->midi_msg_in(p->instance, bus, message);
}

static void message(DSP *dsp, size_t size, const void *data) {
	Perone *p = dsp->instance;
	p->api->msg_in(p->instance, size, data);
}

static int process(DSP *dsp, const float **inputs, float **outputs, size_t frames, const ProcessTime *time) {
	Perone *p = dsp->instance;
	if (time && p->api->set_transport) {
		perone_transport t = {.changed = PERONE_TRANSPORT_PLAYING | PERONE_TRANSPORT_BPM | PERONE_TRANSPORT_QUARTER,
		    .valid = PERONE_TRANSPORT_PLAYING | PERONE_TRANSPORT_BPM | PERONE_TRANSPORT_QUARTER,
		    .playing = time->playing,
		    .bpm = time->bpm,
		    .quarter = time->beat};
		p->api->set_transport(p->instance, &t);
	}
	p->api->process(p->instance, inputs, outputs, frames);
	sync(dsp);
	return 0;
}

static const DspOps ops = {.close = close_instance,
    .set = set,
    .reset = reset,
    .midi = midi,
    .process = process,
    .sync = sync,
    .message = message};

int perone_open(DSP *dsp, unsigned sample_rate, size_t capacity) {
	(void)capacity;
	PeroneModule *module = module_data(dsp->module);
	const perone_api *(*get_api)(uint32_t) = library_symbol(module->library, "perone_get_api");
	const perone_api *a = get_api ? get_api(PERONE_ABI_VERSION) : NULL;
	const PluginConfig *config = &dsp->config;
	if (!a || !a->alloc || !a->free || !a->init || !a->fini || !a->set_sample_rate || !a->mem_req || !a->mem_set ||
	    !a->reset || !a->process || (config->midi >= 0 && !a->midi_msg_in) || (config->to_dsp && !a->msg_in))
		return -1;
	for (int i = 0; i < config->nparams; ++i)
		if ((config->parameters[i].flags & PARAM_OUTPUT) ? !a->get_parameter : !a->set_parameter)
			return -1;
	Perone *p = calloc(1, sizeof(*p));
	if (!p)
		return -1;
	dsp->ops = &ops;
	dsp->instance = p;
	p->api = a;
	perone_callbacks callbacks = {dsp, bindir, datadir, dsp_message};
	if (!(p->instance = a->alloc()) || a->init(p->instance, &callbacks))
		return -1;
	p->initialized = 1;
	for (int i = 0; i < config->nparams; ++i)
		if (!(config->parameters[i].flags & PARAM_OUTPUT))
			a->set_parameter(p->instance, i, config->parameters[i].value);
	a->set_sample_rate(p->instance, sample_rate);
	size_t size = a->mem_req(p->instance);
	if (size) {
		if (!(p->memory = malloc(size)))
			return -1;
		a->mem_set(p->instance, p->memory);
	}
	a->reset(p->instance);
	return 0;
}
