#include "lv2.h"
#include "util.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LV2_URID map_uri(void *handle, const char *uri) {
	Lv2 *l = handle;
	pthread_mutex_lock(&l->uris_mutex);
	uint32_t i = 0;
	for (; i < l->nuri && strcmp(l->uris[i], uri); ++i) {
	}
	if (i == l->nuri) {
		char *copy = copy_string(uri);
		char **next = copy ? realloc(l->uris, (i + 1) * sizeof(*next)) : NULL;
		if (!next) {
			free(copy);
			pthread_mutex_unlock(&l->uris_mutex);
			return 0;
		}
		l->uris = next;
		l->uris[l->nuri++] = copy;
	}
	pthread_mutex_unlock(&l->uris_mutex);
	return i + 1;
}
static const char *unmap_uri(void *handle, LV2_URID id) {
	Lv2 *l = handle;
	pthread_mutex_lock(&l->uris_mutex);
	const char *uri = id && id <= l->nuri ? l->uris[id - 1] : NULL;
	pthread_mutex_unlock(&l->uris_mutex);
	return uri;
}
static int log_vprintf(void *handle, LV2_URID type, const char *fmt, va_list args) {
	(void)handle;
	(void)type;
	(void)fmt;
	(void)args;
	// Plugins may log from run(); do not perform I/O on that thread.
	return 0;
}
static int log_printf(void *handle, LV2_URID type, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int n = log_vprintf(handle, type, fmt, args);
	va_end(args);
	return n;
}
static void sequence_clear(Lv2 *l, Lv2Port *p) {
	if (p->input)
		*p->events = (LV2_Atom_Sequence){.atom = {sizeof(LV2_Atom_Sequence_Body), l->sequence}};
	else
		p->events->atom = (LV2_Atom){LV2_ATOM_CAPACITY - sizeof(LV2_Atom), l->chunk};
}
static int append(Lv2 *l, Lv2Port *port, uint32_t type, uint32_t size, const void *data) {
	LV2_Atom_Sequence *s = port->events;
	size_t total = sizeof(LV2_Atom_Event) + lv2_atom_pad_size(size);
	if (sizeof(LV2_Atom) + s->atom.size + total > LV2_ATOM_CAPACITY) {
		atomic_store(&l->error, 1);
		return -1;
	}
	LV2_Atom_Event *event = (void *)((char *)s + sizeof(LV2_Atom) + s->atom.size);
	*event = (LV2_Atom_Event){.time.frames = 0, .body = {size, type}};
	memcpy(event + 1, data, size);
	memset((char *)(event + 1) + size, 0, lv2_atom_pad_size(size) - size);
	s->atom.size += total;
	l->dirty = 1;
	return 0;
}
int lv2_ui_send(Lv2 *l, uint32_t port, uint32_t size, const void *data) {
	if (port >= l->description.count || l->description.ports[port].type != LV2_PORT_ATOM ||
	    !l->description.ports[port].input || !data || size < sizeof(LV2_Atom) || size > MAX_MESSAGE - sizeof(uint64_t))
		return -1;
	LV2_Atom atom;
	memcpy(&atom, data, sizeof(atom));
	if (!atom.type || atom.size != size - sizeof(atom))
		return -1;
	uint64_t message[MAX_MESSAGE / sizeof(uint64_t)];
	message[0] = port;
	memcpy(message + 1, data, size);
	return message_push(&l->from_ui, size + sizeof(uint64_t), message);
}
static void set(DSP *dsp, size_t index, double value) {
	Lv2 *l = dsp->instance;
	Lv2Port *p = l->description.ports + dsp->config.parameters[index].id;
	p->value = value;
	l->dirty = 1;
}
static void reset(DSP *dsp) {
	Lv2 *l = dsp->instance;
	lv2_worker_wait(l);
	if (l->active)
		lilv_instance_deactivate(l->instance);
	for (uint32_t i = 0; i < l->description.count; ++i)
		if (l->description.ports[i].events)
			sequence_clear(l, l->description.ports + i);
	atomic_store(&l->from_ui.read, atomic_load(&l->from_ui.write));
	atomic_store(&l->to_ui.read, atomic_load(&l->to_ui.write));
	lilv_instance_activate(l->instance);
	l->active = 1;
}
static void midi(DSP *dsp, size_t bus, const uint8_t *message) {
	Lv2 *l = dsp->instance;
	if (bus < l->description.count && l->description.ports[bus].midi)
		append(l, l->description.ports + bus, l->midi, 3, message);
}
static void sync(DSP *dsp) {
	Lv2 *l = dsp->instance;
	uint64_t message[MAX_MESSAGE / sizeof(uint64_t)];
	size_t size;
	for (int i = 0; i < MESSAGE_SLOTS && message_pop(&l->from_ui, &size, message); ++i) {
		uint32_t port = message[0];
		LV2_Atom atom;
		memcpy(&atom, message + 1, sizeof(atom));
		append(l, l->description.ports + port, atom.type, atom.size, (char *)(message + 1) + sizeof(atom));
	}
}
static void position(Lv2 *l, const ProcessTime *time) {
	if (!time)
		return;
	uint64_t buffer[32];
	LV2_Atom_Forge_Frame object;
	lv2_atom_forge_set_buffer(&l->forge, (uint8_t *)buffer, sizeof(buffer));
	lv2_atom_forge_object(&l->forge, &object, 0, l->position);
	lv2_atom_forge_key(&l->forge, l->frame);
	lv2_atom_forge_long(&l->forge, time->frame);
	lv2_atom_forge_key(&l->forge, l->speed);
	lv2_atom_forge_float(&l->forge, time->playing ? 1 : 0);
	lv2_atom_forge_key(&l->forge, l->tempo);
	lv2_atom_forge_float(&l->forge, time->bpm);
	lv2_atom_forge_key(&l->forge, l->beat);
	lv2_atom_forge_double(&l->forge, time->beat);
	lv2_atom_forge_pop(&l->forge, &object);
	const LV2_Atom *atom = (const void *)buffer;
	for (uint32_t i = 0; i < l->description.count; ++i)
		if (l->description.ports[i].time)
			append(l, l->description.ports + i, atom->type, atom->size, atom + 1);
}
static void read_outputs(Lv2 *l) {
	for (uint32_t i = 0; i < l->description.count; ++i) {
		Lv2Port *p = l->description.ports + i;
		if (p->input)
			continue;
		if (p->type == LV2_PORT_CONTROL && isfinite(p->value))
			atomic_store(&l->dsp->values[p->parameter], p->value);
		if (p->type != LV2_PORT_ATOM || !atomic_load(&l->dsp->viewing))
			continue;
		LV2_Atom_Sequence *s = p->events;
		if (s->atom.type != l->sequence || s->atom.size < sizeof(s->body) ||
		    s->atom.size > LV2_ATOM_CAPACITY - sizeof(LV2_Atom))
			continue;
		uint32_t offset = sizeof(*s), end = sizeof(LV2_Atom) + s->atom.size;
		while (end - offset >= sizeof(LV2_Atom_Event)) {
			LV2_Atom_Event *event = (void *)((char *)s + offset);
			if (event->body.size > end - offset - sizeof(*event))
				break;
			size_t size = sizeof(LV2_Atom) + event->body.size;
			uint64_t message[MAX_MESSAGE / sizeof(uint64_t)];
			if (size + sizeof(uint64_t) > sizeof(message)) {
				atomic_store(&l->dsp->overflow, 1);
				break;
			}
			message[0] = i;
			memcpy(message + 1, &event->body, size);
			if (message_push(&l->to_ui, size + sizeof(uint64_t), message))
				atomic_store(&l->dsp->overflow, 1);
			uint32_t next = sizeof(*event) + lv2_atom_pad_size(event->body.size);
			if (next > end - offset)
				break;
			offset += next;
		}
	}
	if (l->description.latency >= 0) {
		float value = l->description.ports[l->description.latency].value;
		if (isfinite(value) && value >= 0 && value < 0x1p32)
			atomic_store(&l->latency, (uint32_t)ceilf(value));
		else
			atomic_store(&l->error, 1);
	}
}
static int process(DSP *dsp, const float **inputs, float **outputs, size_t frames, const ProcessTime *time) {
	Lv2 *l = dsp->instance;
	int offline = time && time->offline;
	if (offline && !l->worker.synchronous)
		lv2_worker_wait(l);
	l->worker.synchronous = offline;
	lv2_worker_emit(l);
	position(l, time);
	for (uint32_t i = 0; i < l->description.count; ++i) {
		Lv2Port *p = l->description.ports + i;
		if (p->freewheel)
			p->value = offline;
		if (p->type != LV2_PORT_AUDIO)
			continue;
		float *data = p->input ? (inputs && inputs[p->channel] ? (float *)inputs[p->channel] : l->scratch)
		    : outputs && p->channel < dsp->config.output ? outputs[p->channel]
		                                                 : l->scratch + (p->channel + 1) * l->max_block;
		lilv_instance_connect_port(l->instance, i, data);
	}
	if (atomic_load(&l->error))
		return -1;
	lilv_instance_run(l->instance, frames);
	l->dirty = 0;
	lv2_worker_emit(l);
	if (l->worker.api && l->worker.api->end_run)
		l->worker.api->end_run(lilv_instance_get_handle(l->instance));
	read_outputs(l);
	for (uint32_t i = 0; i < l->description.count; ++i)
		if (l->description.ports[i].events)
			sequence_clear(l, l->description.ports + i);
	return atomic_load(&l->error) ? -1 : 0;
}
static int poll_instance(DSP *dsp) {
	Lv2 *l = dsp->instance;
	return atomic_load(&l->error) ? -1 : atomic_load(&l->latency) != l->applied_latency;
}
static void idle(DSP *dsp, const ProcessTime *time) {
	Lv2 *l = dsp->instance;
	Messages *q = &l->worker.responses;
	if (l->dirty || (l->worker.started && atomic_load(&q->read) != atomic_load(&q->write)))
		process(dsp, NULL, NULL, 0, time);
}
static int restart(DSP *dsp) {
	Lv2 *l = dsp->instance;
	l->applied_latency = atomic_load(&l->latency);
	return 0;
}
static uint32_t latency(DSP *dsp) {
	return atomic_load(&((Lv2 *)dsp->instance)->latency);
}
static void close_instance(DSP *dsp) {
	Lv2 *l = dsp->instance;
	if (!l)
		return;
	lv2_worker_close(l);
	if (l->active)
		lilv_instance_deactivate(l->instance);
	lilv_instance_free(l->instance);
	for (uint32_t i = 0; l->description.ports && i < l->description.count; ++i)
		free(l->description.ports[i].events);
	free(l->description.ports);
	free(l->scratch);
	free(l->from_ui.data);
	free(l->to_ui.data);
	for (uint32_t i = 0; i < l->nuri; ++i)
		free(l->uris[i]);
	free(l->uris);
	pthread_mutex_destroy(&l->uris_mutex);
	free(l);
}
static const DspOps ops = {.close = close_instance,
    .set = set,
    .reset = reset,
    .midi = midi,
    .process = process,
    .sync = sync,
    .idle = idle,
    .poll = poll_instance,
    .restart = restart,
    .latency = latency};
int lv2_open(DSP *dsp, unsigned rate, size_t capacity) {
	Lv2 *l = calloc(1, sizeof(*l));
	if (!l)
		return -1;
	if (pthread_mutex_init(&l->uris_mutex, NULL)) {
		free(l);
		return -1;
	}
	dsp->instance = l;
	dsp->ops = &ops;
	l->dsp = dsp;
	atomic_init(&l->error, 0);
	atomic_init(&l->latency, 0);
	if (lv2_describe(&l->description, module_data(dsp->module), dsp->config.id))
		return -1;
	Lv2Description *d = &l->description;
	if (d->main_inputs != dsp->config.input || d->main_outputs != dsp->config.output ||
	    d->inputs != dsp->config.inputs || d->midi != dsp->config.midi || d->nparams != dsp->config.nparams)
		return -1;
	for (int i = 0; i < d->nparams; ++i) {
		uint32_t port = dsp->config.parameters[i].id;
		if (port >= d->count || d->ports[port].parameter != i ||
		    d->ports[port].input == !!(dsp->config.parameters[i].flags & PARAM_OUTPUT))
			return -1;
	}
	l->map = (LV2_URID_Map){l, map_uri};
	l->unmap = (LV2_URID_Unmap){l, unmap_uri};
	l->sequence = map_uri(l, LV2_ATOM__Sequence);
	l->chunk = map_uri(l, LV2_ATOM__Chunk);
	l->midi = map_uri(l, LV2_MIDI__MidiEvent);
	l->event_transfer = map_uri(l, LV2_ATOM__eventTransfer);
	l->position = map_uri(l, LV2_TIME__Position);
	l->frame = map_uri(l, LV2_TIME__frame);
	l->speed = map_uri(l, LV2_TIME__speed);
	l->tempo = map_uri(l, LV2_TIME__beatsPerMinute);
	l->beat = map_uri(l, LV2_TIME__beat);
	lv2_atom_forge_init(&l->forge, &l->map);
	l->rate = rate;
	l->scale = 1;
	l->max_block = capacity;
	l->sequence_size = LV2_ATOM_CAPACITY;
	l->log = (LV2_Log_Log){l, log_printf, log_vprintf};
	l->options[0] = (LV2_Options_Option){
	    LV2_OPTIONS_INSTANCE, 0, map_uri(l, LV2_PARAMETERS__sampleRate), sizeof(float), l->forge.Float, &l->rate};
	l->options[1] = (LV2_Options_Option){LV2_OPTIONS_INSTANCE, 0, map_uri(l, LV2_BUF_SIZE__minBlockLength),
	    sizeof(int32_t), l->forge.Int, &l->min_block};
	l->options[2] = (LV2_Options_Option){LV2_OPTIONS_INSTANCE, 0, map_uri(l, LV2_BUF_SIZE__maxBlockLength),
	    sizeof(int32_t), l->forge.Int, &l->max_block};
	l->options[3] = (LV2_Options_Option){LV2_OPTIONS_INSTANCE, 0, map_uri(l, LV2_BUF_SIZE__sequenceSize),
	    sizeof(int32_t), l->forge.Int, &l->sequence_size};
	l->options[4] = (LV2_Options_Option){
	    LV2_OPTIONS_INSTANCE, 0, map_uri(l, LV2_UI__scaleFactor), sizeof(float), l->forge.Float, &l->scale};
	l->features[0] = (LV2_Feature){LV2_URID__map, &l->map};
	l->features[1] = (LV2_Feature){LV2_URID__unmap, &l->unmap};
	l->features[2] = (LV2_Feature){LV2_OPTIONS__options, l->options};
	l->features[3] = (LV2_Feature){LV2_BUF_SIZE__boundedBlockLength, NULL};
	l->features[4] = (LV2_Feature){LV2_LOG__log, &l->log};
	l->features[5] = (LV2_Feature){LV2_CORE__hardRTCapable, NULL};
	l->worker.schedule = (LV2_Worker_Schedule){l, lv2_schedule};
	l->features[6] = (LV2_Feature){LV2_WORKER__schedule, &l->worker.schedule};
	l->features[7] = (LV2_Feature){LV2_STATE__loadDefaultState, NULL};
	for (int i = 0; i < 8; ++i)
		l->feature_ptrs[i] = l->features + i;
	l->instance = lilv_plugin_instantiate(d->plugin, rate, l->feature_ptrs);
	if (!l->instance)
		return -1;
	if (lv2_worker_start(l))
		return -1;
	l->scratch = calloc((d->outputs + 1) * capacity, sizeof(float));
	if (!l->scratch)
		return -1;
	for (int i = 0; i < 2; ++i) {
		Messages *q = i ? &l->to_ui : &l->from_ui;
		q->limit = MAX_MESSAGE;
		q->data = malloc(MESSAGE_SLOTS * q->limit);
		atomic_init(&q->read, 0);
		atomic_init(&q->write, 0);
		if (!q->data)
			return -1;
	}
	for (uint32_t i = 0; i < d->count; ++i) {
		Lv2Port *p = d->ports + i;
		void *data = NULL;
		if (p->type == LV2_PORT_CONTROL) {
			p->value = dsp->config.parameters[p->parameter].value;
			data = &p->value;
		} else if (p->type == LV2_PORT_AUDIO)
			data = l->scratch + (p->input ? 0 : p->channel + 1) * capacity;
		else if (p->type == LV2_PORT_ATOM) {
			p->events = calloc(1, LV2_ATOM_CAPACITY);
			if (!p->events)
				return -1;
			sequence_clear(l, p);
			data = p->events;
		}
		lilv_instance_connect_port(l->instance, i, data);
	}
	if (lv2_is(d->world, lilv_plugin_get_uri(d->plugin), LV2_CORE__requiredFeature, LV2_STATE__loadDefaultState) ||
	    lv2_is(d->world, lilv_plugin_get_uri(d->plugin), LV2_CORE__optionalFeature, LV2_STATE__loadDefaultState)) {
		LilvState *state = lilv_state_new_from_world(d->world, &l->map, lilv_plugin_get_uri(d->plugin));
		if (!state)
			return -1;
		lilv_state_restore(state, l->instance, NULL, NULL, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, l->feature_ptrs);
		lilv_state_free(state);
	}
	reset(dsp);
	if (process(dsp, NULL, NULL, 0, &(ProcessTime){.offline = 1}))
		return -1;
	restart(dsp);
	return 0;
}
