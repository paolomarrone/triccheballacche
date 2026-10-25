#include "dsp_internal.h"
#include "perone.h"
#include "platform.h"
#include "plugin.h"
#include <math.h>
#include <string.h>
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
	float rate;
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

static void midi(DSP *dsp, size_t bus, const uint8_t *message, size_t size) {
	Perone *p = dsp->instance;
	if (size <= 3) {
		uint8_t bytes[3] = {0};
		memcpy(bytes, message, size);
		p->api->midi_msg_in(p->instance, bus, bytes);
	}
}

static void message(DSP *dsp, size_t size, const void *data) {
	Perone *p = dsp->instance;
	p->api->msg_in(p->instance, size, data);
}

static void transport(DSP *dsp, const ProcessTime *time) {
	Perone *p = dsp->instance;
	if (time && p->api->set_transport) {
		perone_transport t = {.changed = PERONE_TRANSPORT_PLAYING | PERONE_TRANSPORT_BPM | PERONE_TRANSPORT_QUARTER,
		    .valid = PERONE_TRANSPORT_PLAYING | PERONE_TRANSPORT_BPM | PERONE_TRANSPORT_QUARTER,
		    .playing = time->playing,
		    .bpm = time->bpm,
		    .quarter = time->beat};
		p->api->set_transport(p->instance, &t);
	}
}

static int process(DSP *dsp, const float **inputs, float **outputs, size_t frames, const ProcessTime *time) {
	Perone *p = dsp->instance;
	transport(dsp, time);
	p->api->process(p->instance, inputs, outputs, frames);
	sync(dsp);
	return 0;
}

typedef struct {
	DSP *dsp;
	State *buffer;
	double *values;
	int failed;
} PeroneState;

static void state_lock(void *handle) {
	(void)handle; // The caller has already stopped audio.
}
static int state_output(void *handle, const char *data, size_t size) {
	PeroneState *s = handle;
	if (!s->buffer) {
		s->failed = 1;
		return -1;
	}
	return state_write(s->buffer, data, size);
}
static void state_parameter(void *handle, size_t index, float value) {
	PeroneState *s = handle;
	double adjusted = value;
	if (!s->values || plugin_parameter(&s->dsp->config, index, &adjusted))
		s->failed = 1;
	else
		s->values[index] = adjusted;
}
static int state(DSP *dsp, State *buffer, int write) {
	Perone *p = dsp->instance;
	if (!!p->api->state_save != !!p->api->state_load)
		return STATE_UNSUPPORTED;
	uint32_t version = 0x31524550, custom = !!p->api->state_save; // PER1, little endian.
	if (state_u32(buffer, &version, write) || version != 0x31524550 || state_u32(buffer, &custom, write) ||
	    custom != !!p->api->state_save)
		return -1;
	PeroneState s = {.dsp = dsp, .buffer = write ? buffer : NULL};
	perone_state_callbacks callbacks = {&s, state_lock, state_lock, state_output, state_parameter};
	if (write && custom) {
		int result = p->api->state_save(p->instance, &callbacks, p->rate);
		return result || s.failed ? -1 : 0;
	}
	s.values = dsp->config.nparams ? malloc(dsp->config.nparams * sizeof(double)) : NULL;
	if (dsp->config.nparams && !s.values)
		return -1;
	for (int i = 0; i < dsp->config.nparams; ++i)
		s.values[i] = NAN;
	if (custom) {
		if (p->api->state_load(
		        &callbacks, p->rate, (const char *)buffer->data + buffer->position, buffer->length - buffer->position))
			s.failed = 1;
	} else {
		for (int i = 0; i < dsp->config.nparams; ++i) {
			float value = dsp->config.parameters[i].flags & PARAM_OUTPUT ? 0 : atomic_load(&dsp->values[i]);
			uint32_t bits;
			memcpy(&bits, &value, sizeof(bits));
			if (state_u32(buffer, &bits, write)) {
				s.failed = 1;
				break;
			}
			memcpy(&value, &bits, sizeof(value));
			if (!write && !(dsp->config.parameters[i].flags & PARAM_OUTPUT))
				state_parameter(&s, i, value);
		}
		if (!write && buffer->position != buffer->length)
			s.failed = 1;
	}
	if (!write && !s.failed)
		for (int i = 0; i < dsp->config.nparams; ++i)
			if (!isnan(s.values[i]))
				set_dsp(dsp, i, s.values[i]);
	free(s.values);
	return s.failed ? -1 : 0;
}

static const DspOps ops = {.close = close_instance,
    .set = set,
    .reset = reset,
    .midi = midi,
    .process = process,
    .sync = sync,
    .idle = transport,
    .message = message,
    .state = state};

int perone_open(DSP *dsp, unsigned sample_rate, size_t capacity) {
	(void)capacity;
	PeroneModule *module = module_data(dsp->module);
	const perone_api *(*get_api)(uint32_t) = library_symbol(module->library, "perone_get_api");
	const perone_api *a = get_api ? get_api(PERONE_ABI_VERSION) : NULL;
	const PluginConfig *config = &dsp->config;
	if (config->midi_output || !a || !a->alloc || !a->free || !a->init || !a->fini || !a->set_sample_rate ||
	    !a->mem_req || !a->mem_set || !a->reset || !a->process || (config->midi >= 0 && !a->midi_msg_in) ||
	    (config->to_dsp && !a->msg_in))
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
	p->rate = sample_rate;
	perone_callbacks callbacks = {dsp, bindir, datadir, dsp_message};
	if (!(p->instance = a->alloc()) || a->init(p->instance, &callbacks))
		return -1;
	p->initialized = 1;
	// Parameter setters may derive coefficients from the sample rate.
	a->set_sample_rate(p->instance, sample_rate);
	for (int i = 0; i < config->nparams; ++i)
		if (!(config->parameters[i].flags & PARAM_OUTPUT))
			a->set_parameter(p->instance, i, config->parameters[i].value);
	size_t size = a->mem_req(p->instance);
	if (size) {
		if (!(p->memory = malloc(size)))
			return -1;
		a->mem_set(p->instance, p->memory);
	}
	a->reset(p->instance);
	return 0;
}
