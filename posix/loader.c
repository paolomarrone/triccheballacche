#include "module.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static const char *bindir(void *handle) {
	return ((DSP *)handle)->bindir;
}

static const char *datadir(void *handle) {
	return ((DSP *)handle)->datadir;
}

DSP *open_dsp(const char *path, const PluginConfig *config, unsigned sample_rate, size_t capacity) {
	(void)capacity;
	DSP *m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;
	const char *error = "cannot resolve plugin binary";
	m->bindir = realpath(path, NULL);
	if (!m->bindir)
		goto fail;
	*strrchr(m->bindir, '/') = 0;
	m->datadir = copy_string(m->bindir);
	if (!m->datadir) {
		error = "out of memory";
		goto fail;
	}
	char *slash = strrchr(m->datadir, '/');
	if (!slash)
		goto fail;
	*slash = 0;
	m->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!m->handle) {
		error = dlerror();
		goto fail;
	}
	const perone_api *(*get_api)(uint32_t) = dlsym(m->handle, "perone_get_api");
	if (!get_api || !(m->api = get_api(PERONE_ABI_VERSION))) {
		error = "unsupported Perone ABI";
		goto fail;
	}
	const perone_api *a = m->api;
	error = "missing Perone function";
	if (!a->alloc || !a->free || !a->init || !a->fini || !a->set_sample_rate || !a->mem_req || !a->mem_set ||
	    !a->reset || !a->process || (config->midi >= 0 && !a->midi_msg_in))
		goto fail;
	for (int i = 0; i < config->nparams; ++i)
		if ((config->outputs & (UINT64_C(1) << i)) ? !a->get_parameter : !a->set_parameter)
			goto fail;
	error = "cannot initialize Perone instance";
	perone_callbacks callbacks = {m, bindir, datadir, NULL};
	if (!(m->instance = a->alloc()) || a->init(m->instance, &callbacks))
		goto fail;
	m->initialized = 1;
	for (int i = 0; i < config->nparams; ++i)
		if (!(config->outputs & (UINT64_C(1) << i)))
			a->set_parameter(m->instance, i, config->defaults[i]);
	a->set_sample_rate(m->instance, sample_rate);
	size_t size = a->mem_req(m->instance);
	if (size) {
		if (!(m->memory = malloc(size)))
			goto fail;
		a->mem_set(m->instance, m->memory);
	}
	a->reset(m->instance);
	return m;
fail:
	fprintf(stderr, "[Loader] %s: %s\n", path, error);
	close_dsp(m);
	return NULL;
}

void close_dsp(DSP *dsp) {
	if (!dsp)
		return;
	if (dsp->initialized)
		dsp->api->fini(dsp->instance);
	free(dsp->memory);
	if (dsp->instance)
		dsp->api->free(dsp->instance);
	if (dsp->handle)
		dlclose(dsp->handle);
	free(dsp->bindir);
	free(dsp->datadir);
	free(dsp);
}

void set_dsp(DSP *dsp, size_t parameter, float value) {
	dsp->api->set_parameter(dsp->instance, parameter, value);
}

void reset_dsp(DSP *dsp) {
	dsp->api->reset(dsp->instance);
}

void midi_dsp(DSP *dsp, size_t bus, const uint8_t *message) {
	dsp->api->midi_msg_in(dsp->instance, bus, message);
}

void process_dsp(DSP *dsp, const float **inputs, float **outputs, size_t frames) {
	dsp->api->process(dsp->instance, inputs, outputs, frames);
}
