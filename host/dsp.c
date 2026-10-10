#include "dsp_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void dsp_message(void *handle, size_t size, const void *data) {
	DSP *dsp = handle;
	if (atomic_load(&dsp->viewing) && message_push(&dsp->to_ui, size, data))
		atomic_store(&dsp->overflow, 1);
}

void watch_dsp(DSP *dsp, int watching) {
	// A new attachment starts a fresh output stream; accepted input edits remain pending.
	atomic_store(&dsp->to_ui.read, atomic_load(&dsp->to_ui.write));
	atomic_store(&dsp->overflow, 0);
	atomic_store(&dsp->viewing, watching);
	if (watching)
		atomic_store(&dsp->pending, 1);
}

void edit_dsp(DSP *dsp, size_t parameter, double value) {
	atomic_store(&dsp->wanted[parameter], value);
	atomic_fetch_add(&dsp->requested[parameter], 1);
	atomic_store(&dsp->pending, 1);
}

int send_dsp(DSP *dsp, size_t size, const void *data) {
	int result = message_push(&dsp->to_dsp, size, data);
	if (!result)
		atomic_store(&dsp->pending, 1);
	return result;
}

int read_dsp(DSP *dsp, size_t parameter, double *value) {
	if (atomic_load(&dsp->applied[parameter]) != atomic_load(&dsp->requested[parameter]))
		return 0;
	*value = atomic_load(&dsp->values[parameter]);
	return 1;
}

int receive_dsp(DSP *dsp, size_t *size, void *data) {
	return atomic_load(&dsp->overflow) ? -1 : message_pop(&dsp->to_ui, size, data);
}

DSP *open_dsp(Modules *modules, const char *path, const PluginConfig *config, unsigned rate, size_t capacity) {
	DSP *dsp = calloc(1, sizeof(*dsp));
	if (!dsp)
		return NULL;
	dsp->config = *config;
	size_t n = config->nparams;
	dsp->requested = calloc(n, sizeof(*dsp->requested));
	dsp->applied = calloc(n, sizeof(*dsp->applied));
	dsp->wanted = calloc(n, sizeof(*dsp->wanted));
	dsp->values = calloc(n, sizeof(*dsp->values));
	if (n && (!dsp->requested || !dsp->applied || !dsp->wanted || !dsp->values))
		goto fail;
	atomic_init(&dsp->viewing, 0);
	atomic_init(&dsp->pending, 0);
	atomic_init(&dsp->overflow, 0);
	for (size_t i = 0; i < n; ++i) {
		atomic_init(&dsp->requested[i], 0);
		atomic_init(&dsp->applied[i], 0);
		atomic_init(&dsp->wanted[i], config->parameters[i].value);
		atomic_init(&dsp->values[i], config->parameters[i].value);
	}
	dsp->to_ui.limit = config->to_ui;
	dsp->to_dsp.limit = config->to_dsp;
	atomic_init(&dsp->to_ui.read, 0);
	atomic_init(&dsp->to_ui.write, 0);
	atomic_init(&dsp->to_dsp.read, 0);
	atomic_init(&dsp->to_dsp.write, 0);
	if (config->to_ui > MAX_MESSAGE || config->to_dsp > MAX_MESSAGE)
		goto fail;
	if ((config->to_ui && !(dsp->to_ui.data = malloc(MESSAGE_SLOTS * config->to_ui))) ||
	    (config->to_dsp && !(dsp->to_dsp.data = malloc(MESSAGE_SLOTS * config->to_dsp))))
		goto fail;
	dsp->module = module_open(modules, path, config->format);
	if (!dsp->module)
		goto fail;
	int (*const openers[])(DSP *, unsigned, size_t) = {perone_open, clap_open, lv2_open};
	if (config->format < 0 || config->format >= PLUGIN_FORMATS || openers[config->format](dsp, rate, capacity))
		goto fail;
	return dsp;
fail:
	fprintf(stderr, "[Plugin] %s: cannot initialize instance\n", path);
	close_dsp(dsp);
	return NULL;
}

void close_dsp(DSP *dsp) {
	if (!dsp)
		return;
	if (dsp->ops)
		dsp->ops->close(dsp);
	module_close(dsp->module);
	free(dsp->to_ui.data);
	free(dsp->to_dsp.data);
	free(dsp->requested);
	free(dsp->applied);
	free(dsp->wanted);
	free(dsp->values);
	free(dsp);
}

void set_dsp(DSP *dsp, size_t parameter, double value) {
	dsp->ops->set(dsp, parameter, value);
	atomic_store(&dsp->values[parameter], value);
}

void reset_dsp(DSP *dsp) {
	atomic_store(&dsp->pending, 0);
	atomic_store(&dsp->overflow, 0);
	atomic_store(&dsp->to_ui.read, atomic_load(&dsp->to_ui.write));
	atomic_store(&dsp->to_dsp.read, atomic_load(&dsp->to_dsp.write));
	for (int i = 0; i < dsp->config.nparams; ++i)
		atomic_store(&dsp->applied[i], atomic_load(&dsp->requested[i]));
	dsp->ops->reset(dsp);
}

void midi_dsp(DSP *dsp, size_t bus, const uint8_t *message) {
	dsp->ops->midi(dsp, bus, message);
}
void idle_dsp(DSP *dsp, const ProcessTime *time) {
	if (dsp && dsp->ops->idle)
		dsp->ops->idle(dsp, time);
}
uint32_t latency_dsp(DSP *dsp) {
	return dsp && dsp->ops->latency ? dsp->ops->latency(dsp) : 0;
}
int poll_dsp(DSP *dsp) {
	return dsp && dsp->ops->poll ? dsp->ops->poll(dsp) : 0;
}
int restart_dsp(DSP *dsp) {
	return dsp && dsp->ops->restart ? dsp->ops->restart(dsp) : 0;
}
int state_dsp(DSP *dsp, const char *path, int write) {
	return dsp && dsp->ops->state ? dsp->ops->state(dsp, path, write) : -1;
}
int process_dsp(DSP *dsp, const float **inputs, float **outputs, size_t frames, const ProcessTime *time) {
	return dsp->ops->process(dsp, inputs, outputs, frames, time);
}

void sync_dsp(DSP *dsp, DSP *paired) {
	if (!dsp)
		return;
	if (!atomic_exchange(&dsp->pending, 0))
		goto sync;
	for (int i = 0; i < dsp->config.nparams; ++i) {
		unsigned request = atomic_load(&dsp->requested[i]);
		if (request == atomic_load(&dsp->applied[i]))
			continue;
		double value = atomic_load(&dsp->wanted[i]);
		set_dsp(dsp, i, value);
		if (paired)
			set_dsp(paired, i, value);
		// A newer UI request must stay pending, including one arriving during set_parameter.
		atomic_store(&dsp->applied[i], request);
	}
	size_t size;
	unsigned char data[MAX_MESSAGE];
	for (int i = 0; i < MESSAGE_SLOTS && message_pop(&dsp->to_dsp, &size, data); ++i) {
		if (dsp->ops->message)
			dsp->ops->message(dsp, size, data);
		if (paired)
			if (paired->ops->message)
				paired->ops->message(paired, size, data);
	}
sync:
	if (dsp->ops->sync)
		dsp->ops->sync(dsp);
	if (paired && paired->ops->sync)
		paired->ops->sync(paired);
}
