#include "module.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

const size_t dsp_max_message = MAX_MESSAGE;

int message_push(Messages *q, size_t size, const void *data) {
	unsigned w = atomic_load_explicit(&q->write, memory_order_relaxed);
	if (!q->data || size > q->limit || w - atomic_load_explicit(&q->read, memory_order_acquire) == MESSAGE_SLOTS)
		return -1;
	if (size)
		memcpy(q->data + (w % MESSAGE_SLOTS) * q->limit, data, size);
	q->sizes[w % MESSAGE_SLOTS] = size;
	atomic_store_explicit(&q->write, w + 1, memory_order_release);
	return 0;
}

int message_pop(Messages *q, size_t *size, void *data) {
	unsigned r = atomic_load_explicit(&q->read, memory_order_relaxed);
	if (r == atomic_load_explicit(&q->write, memory_order_acquire))
		return 0;
	*size = q->sizes[r % MESSAGE_SLOTS];
	if (*size)
		memcpy(data, q->data + (r % MESSAGE_SLOTS) * q->limit, *size);
	atomic_store_explicit(&q->read, r + 1, memory_order_release);
	return 1;
}

static void message(void *handle, size_t size, const void *data) {
	DSP *dsp = handle;
	if (atomic_load(&dsp->viewing) && message_push(&dsp->to_ui, size, data))
		atomic_store(&dsp->overflow, 1);
}

void watch_dsp(DSP *dsp, int watching) {
	atomic_store(&dsp->viewing, watching);
}

void edit_dsp(DSP *dsp, size_t parameter, float value) {
	atomic_store(&dsp->wanted[parameter], value);
	atomic_fetch_add(&dsp->requested[parameter], 1);
}

int send_dsp(DSP *dsp, size_t size, const void *data) {
	return message_push(&dsp->to_dsp, size, data);
}

int read_dsp(DSP *dsp, size_t parameter, float *value) {
	if (atomic_load(&dsp->applied[parameter]) != atomic_load(&dsp->requested[parameter]))
		return 0;
	*value = atomic_load(&dsp->values[parameter]);
	return 1;
}

int receive_dsp(DSP *dsp, size_t *size, void *data) {
	return atomic_load(&dsp->overflow) ? -1 : message_pop(&dsp->to_ui, size, data);
}

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
	m->config = *config;
	atomic_init(&m->viewing, 0);
	atomic_init(&m->overflow, 0);
	for (int i = 0; i < MAX_PARAMS; ++i) {
		atomic_init(&m->requested[i], 0);
		atomic_init(&m->applied[i], 0);
		atomic_init(&m->wanted[i], config->defaults[i]);
		atomic_init(&m->values[i], config->defaults[i]);
	}
	atomic_init(&m->to_ui.read, 0);
	atomic_init(&m->to_ui.write, 0);
	atomic_init(&m->to_dsp.read, 0);
	atomic_init(&m->to_dsp.write, 0);
	m->to_ui.limit = config->to_ui;
	m->to_dsp.limit = config->to_dsp;
	const char *error = "cannot resolve plugin binary";
	if (config->to_ui > MAX_MESSAGE || config->to_dsp > MAX_MESSAGE) {
		error = "unsupported message size";
		goto fail;
	}
	if ((config->to_ui && !(m->to_ui.data = malloc(MESSAGE_SLOTS * config->to_ui))) ||
	    (config->to_dsp && !(m->to_dsp.data = malloc(MESSAGE_SLOTS * config->to_dsp)))) {
		error = "out of memory";
		goto fail;
	}
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
	    !a->reset || !a->process || (config->midi >= 0 && !a->midi_msg_in) || (config->to_dsp && !a->msg_in))
		goto fail;
	for (int i = 0; i < config->nparams; ++i)
		if ((config->outputs & (UINT64_C(1) << i)) ? !a->get_parameter : !a->set_parameter)
			goto fail;
	error = "cannot initialize Perone instance";
	perone_callbacks callbacks = {m, bindir, datadir, message};
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
	free(dsp->to_ui.data);
	free(dsp->to_dsp.data);
	free(dsp);
}

void set_dsp(DSP *dsp, size_t parameter, float value) {
	dsp->api->set_parameter(dsp->instance, parameter, value);
	atomic_store(&dsp->values[parameter], value);
}

void reset_dsp(DSP *dsp) {
	dsp->api->reset(dsp->instance);
}

void midi_dsp(DSP *dsp, size_t bus, const uint8_t *message) {
	dsp->api->midi_msg_in(dsp->instance, bus, message);
}

void sync_dsp(DSP *dsp, DSP *paired) {
	if (!dsp || !atomic_load(&dsp->viewing))
		return;
	for (int i = 0; i < dsp->config.nparams; ++i) {
		unsigned request = atomic_load(&dsp->requested[i]);
		if (request == atomic_load(&dsp->applied[i]))
			continue;
		float value = atomic_load(&dsp->wanted[i]);
		set_dsp(dsp, i, value);
		if (paired)
			set_dsp(paired, i, value);
		// A newer UI request must stay pending, including one arriving during set_parameter.
		atomic_store(&dsp->applied[i], request);
	}
	size_t size;
	unsigned char data[MAX_MESSAGE];
	for (int i = 0; i < MESSAGE_SLOTS && message_pop(&dsp->to_dsp, &size, data); ++i) {
		dsp->api->msg_in(dsp->instance, size, data);
		if (paired)
			paired->api->msg_in(paired->instance, size, data);
	}
}

void process_dsp(DSP *dsp, const float **inputs, float **outputs, size_t frames) {
	dsp->api->process(dsp->instance, inputs, outputs, frames);
	if (atomic_load(&dsp->viewing))
		for (int i = 0; i < dsp->config.nparams; ++i)
			if (dsp->config.outputs & (UINT64_C(1) << i))
				atomic_store(&dsp->values[i], dsp->api->get_parameter(dsp->instance, i));
}
