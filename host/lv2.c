#include "lv2.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
		p->events->atom = (LV2_Atom){p->capacity - sizeof(LV2_Atom), l->chunk};
}
static int append(Lv2 *l, Lv2Port *port, uint32_t type, uint32_t size, const void *data) {
	LV2_Atom_Sequence *s = port->events;
	size_t total = sizeof(LV2_Atom_Event) + lv2_atom_pad_size(size);
	if (sizeof(LV2_Atom) + s->atom.size + total > port->capacity)
		return -1;
	LV2_Atom_Event *event = (void *)((char *)s + sizeof(LV2_Atom) + s->atom.size);
	*event = (LV2_Atom_Event){.time.frames = 0, .body = {size, type}};
	memcpy(event + 1, data, size);
	memset((char *)(event + 1) + size, 0, lv2_atom_pad_size(size) - size);
	s->atom.size += total;
	l->dirty = 1;
	return 0;
}
static const LV2_Atom *position_atom(const Lv2 *l, const ProcessTime *time, uint64_t buffer[32]) {
	LV2_Atom_Forge forge = l->forge;
	LV2_Atom_Forge_Frame object;
	lv2_atom_forge_set_buffer(&forge, (uint8_t *)buffer, 32 * sizeof(*buffer));
	lv2_atom_forge_object(&forge, &object, 0, l->position);
	lv2_atom_forge_key(&forge, l->frame);
	lv2_atom_forge_long(&forge, time->frame);
	lv2_atom_forge_key(&forge, l->speed);
	lv2_atom_forge_float(&forge, time->playing ? 1 : 0);
	lv2_atom_forge_key(&forge, l->tempo);
	lv2_atom_forge_float(&forge, time->bpm);
	lv2_atom_forge_key(&forge, l->beat);
	lv2_atom_forge_double(&forge, time->beat);
	lv2_atom_forge_pop(&forge, &object);
	return (const void *)buffer;
}
int lv2_ui_send(Lv2 *l, uint32_t port, uint32_t size, const void *data) {
	if (port >= l->description.count || l->description.ports[port].type != LV2_PORT_ATOM ||
	    !l->description.ports[port].input || !data || size < sizeof(LV2_Atom) ||
	    size > l->from_ui.limit - sizeof(uint64_t))
		return -1;
	LV2_Atom atom;
	memcpy(&atom, data, sizeof(atom));
	if (!atom.type || atom.size != size - sizeof(atom))
		return -1;
	Lv2Port *p = l->description.ports + port;
	size_t needed = sizeof(LV2_Atom_Sequence) + sizeof(LV2_Atom_Event) + lv2_atom_pad_size(atom.size);
	if (p->time) {
		uint64_t buffer[32];
		const LV2_Atom *pos = position_atom(l, &(ProcessTime){0}, buffer);
		needed += sizeof(LV2_Atom_Event) + lv2_atom_pad_size(pos->size);
	}
	if (needed > p->capacity)
		return -1;
	uint64_t *message = l->ui_message;
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
static void midi(DSP *dsp, size_t bus, const uint8_t *message, size_t size) {
	Lv2 *l = dsp->instance;
	if (bus < l->description.count && l->description.ports[bus].midi &&
	    append(l, l->description.ports + bus, l->midi, size, message))
		atomic_store(&l->error, 1);
}
static void ui_events(Lv2 *l) {
	size_t size;
	for (int i = 0; i < MESSAGE_SLOTS; ++i) {
		const uint64_t *message = message_peek(&l->from_ui, &size);
		if (!message)
			break;
		uint32_t port = message[0];
		LV2_Atom atom;
		memcpy(&atom, message + 1, sizeof(atom));
		// Leave the next message queued if this block is full. Preserve order across ports.
		if (append(l, l->description.ports + port, atom.type, atom.size, (const char *)(message + 1) + sizeof(atom)))
			break;
		message_pop(&l->from_ui, &size, NULL);
	}
}
static void position(Lv2 *l, const ProcessTime *time) {
	if (!time)
		return;
	uint64_t buffer[32];
	const LV2_Atom *atom = position_atom(l, time, buffer);
	for (uint32_t i = 0; i < l->description.count; ++i)
		if (l->description.ports[i].time && append(l, l->description.ports + i, atom->type, atom->size, atom + 1))
			atomic_store(&l->error, 1);
}
static void read_outputs(Lv2 *l) {
	for (uint32_t i = 0; i < l->description.count; ++i) {
		Lv2Port *p = l->description.ports + i;
		if (p->input)
			continue;
		if (p->type == LV2_PORT_CONTROL && isfinite(p->value))
			atomic_store(&l->dsp->values[p->parameter], p->value);
		if (p->type != LV2_PORT_ATOM)
			continue;
		LV2_Atom_Sequence *s = p->events;
		if (s->atom.type != l->sequence || s->atom.size < sizeof(s->body) ||
		    s->atom.size > p->capacity - sizeof(LV2_Atom))
			continue;
		uint32_t offset = sizeof(*s), end = sizeof(LV2_Atom) + s->atom.size;
		while (end - offset >= sizeof(LV2_Atom_Event)) {
			LV2_Atom_Event *event = (void *)((char *)s + offset);
			if (event->body.size > end - offset - sizeof(*event))
				break;
			if (p->midi && event->body.type == l->midi) {
				if ((s->body.unit && s->body.unit != l->frame_time) || event->time.frames < 0 ||
				    event->time.frames > UINT32_MAX ||
				    dsp_midi_output(l->dsp, event->time.frames, (const uint8_t *)(event + 1), event->body.size))
					atomic_store(&l->error, 1);
			}
			size_t size = sizeof(LV2_Atom) + event->body.size;
			if (atomic_load(&l->dsp->viewing)) {
				uint64_t *message = l->audio_message;
				if (size + sizeof(uint64_t) > l->to_ui.limit) {
					atomic_store(&l->dsp->overflow, 1);
					break;
				}
				message[0] = i;
				memcpy(message + 1, &event->body, size);
				if (message_push(&l->to_ui, size + sizeof(uint64_t), message))
					atomic_store(&l->dsp->overflow, 1);
			}
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
	// Zero-frame calls initialize the plugin or service stopped controls on the main thread.
	lv2_worker_begin(l, !frames || l->offline);
	position(l, time);
	ui_events(l);
	for (uint32_t i = 0; i < l->description.count; ++i) {
		Lv2Port *p = l->description.ports + i;
		if (p->freewheel)
			p->value = l->offline;
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
	lv2_worker_end(l);
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
	size_t size;
	if (l->dirty || message_peek(&l->from_ui, &size) || lv2_worker_pending(l))
		process(dsp, NULL, NULL, 0, time);
}
static int restart(DSP *dsp) {
	Lv2 *l = dsp->instance;
	l->applied_latency = atomic_load(&l->latency);
	return 0;
}
static int offline(DSP *dsp, int enabled) {
	Lv2 *l = dsp->instance;
	if (atomic_load(&l->error))
		return -1;
	if (l->offline == enabled)
		return 0;
	l->offline = enabled;
	if (process(dsp, NULL, NULL, 0, NULL))
		return -1;
	return restart(dsp);
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
	free(l->audio_message);
	free(l->ui_message);
	lv2_runtime_close(l);
	free(l);
}
static const DspOps ops = {.close = close_instance,
    .state = lv2_state,
    .set = set,
    .reset = reset,
    .midi = midi,
    .process = process,
    .idle = idle,
    .poll = poll_instance,
    .restart = restart,
    .offline = offline,
    .latency = latency};
int lv2_open(DSP *dsp, unsigned rate, size_t capacity) {
	Lv2 *l = calloc(1, sizeof(*l));
	if (!l)
		return -1;
	if (lv2_runtime_open(l)) {
		free(l);
		return -1;
	}
	dsp->instance = l;
	dsp->ops = &ops;
	l->dsp = dsp;
	atomic_init(&l->error, 0);
	atomic_init(&l->latency, 0);
	if (lv2_describe(&l->description, module_data(dsp->module), dsp->config.id)) {
		fprintf(stderr, "[LV2] %s: %s\n", dsp->config.id, l->description.error);
		return -1;
	}
	Lv2Description *d = &l->description;
	if (d->main_inputs != dsp->config.input || d->main_outputs != dsp->config.output ||
	    d->midi_output != dsp->config.midi_output || d->inputs != dsp->config.inputs || d->midi != dsp->config.midi ||
	    d->nparams != dsp->config.nparams)
		return -1;
	for (int i = 0; i < d->nparams; ++i) {
		uint32_t port = dsp->config.parameters[i].id;
		if (port >= d->count || d->ports[port].parameter != i ||
		    d->ports[port].input == !!(dsp->config.parameters[i].flags & PARAM_OUTPUT))
			return -1;
	}
	l->map = (LV2_URID_Map){l, lv2_map_uri};
	l->unmap = (LV2_URID_Unmap){l, lv2_unmap_uri};
	l->sequence = lv2_map_uri(l, LV2_ATOM__Sequence);
	l->frame_time = lv2_map_uri(l, LV2_ATOM__frameTime);
	l->chunk = lv2_map_uri(l, LV2_ATOM__Chunk);
	l->midi = lv2_map_uri(l, LV2_MIDI__MidiEvent);
	l->event_transfer = lv2_map_uri(l, LV2_ATOM__eventTransfer);
	l->position = lv2_map_uri(l, LV2_TIME__Position);
	l->frame = lv2_map_uri(l, LV2_TIME__frame);
	l->speed = lv2_map_uri(l, LV2_TIME__speed);
	l->tempo = lv2_map_uri(l, LV2_TIME__beatsPerMinute);
	l->beat = lv2_map_uri(l, LV2_TIME__beat);
	lv2_atom_forge_init(&l->forge, &l->map);
	l->rate = rate;
	l->scale = 1;
	l->max_block = capacity;
	l->sequence_size = LV2_ATOM_DEFAULT;
	for (uint32_t i = 0; i < d->count; ++i) {
		Lv2Port *p = d->ports + i;
		if (p->type != LV2_PORT_ATOM)
			continue;
		if (p->capacity > (uint32_t)l->sequence_size)
			l->sequence_size = p->capacity;
		Messages *q = p->input ? &l->from_ui : &l->to_ui;
		if (q->limit < p->capacity + sizeof(uint64_t))
			q->limit = p->capacity + sizeof(uint64_t);
	}
	l->log = (LV2_Log_Log){l, log_printf, log_vprintf};
	l->options[0] = (LV2_Options_Option){
	    LV2_OPTIONS_INSTANCE, 0, lv2_map_uri(l, LV2_PARAMETERS__sampleRate), sizeof(float), l->forge.Float, &l->rate};
	l->options[1] = (LV2_Options_Option){LV2_OPTIONS_INSTANCE, 0, lv2_map_uri(l, LV2_BUF_SIZE__minBlockLength),
	    sizeof(int32_t), l->forge.Int, &l->min_block};
	l->options[2] = (LV2_Options_Option){LV2_OPTIONS_INSTANCE, 0, lv2_map_uri(l, LV2_BUF_SIZE__maxBlockLength),
	    sizeof(int32_t), l->forge.Int, &l->max_block};
	l->options[3] = (LV2_Options_Option){LV2_OPTIONS_INSTANCE, 0, lv2_map_uri(l, LV2_BUF_SIZE__sequenceSize),
	    sizeof(int32_t), l->forge.Int, &l->sequence_size};
	l->options[4] = (LV2_Options_Option){
	    LV2_OPTIONS_INSTANCE, 0, lv2_map_uri(l, LV2_UI__scaleFactor), sizeof(float), l->forge.Float, &l->scale};
	l->features[0] = (LV2_Feature){LV2_URID__map, &l->map};
	l->features[1] = (LV2_Feature){LV2_URID__unmap, &l->unmap};
	l->features[2] = (LV2_Feature){LV2_OPTIONS__options, l->options};
	l->features[3] = (LV2_Feature){LV2_BUF_SIZE__boundedBlockLength, NULL};
	l->features[4] = (LV2_Feature){LV2_LOG__log, &l->log};
	l->features[5] = (LV2_Feature){LV2_CORE__hardRTCapable, NULL};
	l->schedule = (LV2_Worker_Schedule){l, lv2_schedule};
	l->features[6] = (LV2_Feature){LV2_WORKER__schedule, &l->schedule};
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
		q->data = q->limit ? malloc(MESSAGE_SLOTS * q->limit) : NULL;
		atomic_init(&q->read, 0);
		atomic_init(&q->write, 0);
		if (q->limit && !q->data)
			return -1;
	}
	l->audio_message = l->to_ui.limit ? malloc(l->to_ui.limit) : NULL;
	l->ui_message = l->from_ui.limit ? malloc(l->from_ui.limit) : NULL;
	if ((l->to_ui.limit && !l->audio_message) || (l->from_ui.limit && !l->ui_message))
		return -1;
	for (uint32_t i = 0; i < d->count; ++i) {
		Lv2Port *p = d->ports + i;
		void *data = NULL;
		if (p->type == LV2_PORT_CONTROL) {
			p->value = dsp->config.parameters[p->parameter].value;
			data = &p->value;
		} else if (p->type == LV2_PORT_AUDIO)
			data = l->scratch + (p->input ? 0 : p->channel + 1) * capacity;
		else if (p->type == LV2_PORT_ATOM) {
			p->events = calloc(1, p->capacity);
			if (!p->events)
				return -1;
			sequence_clear(l, p);
			data = p->events;
		}
		lilv_instance_connect_port(l->instance, i, data);
	}
	if (lv2_default_state(d)) {
		LilvState *state = lilv_state_new_from_world(d->world, &l->map, lilv_plugin_get_uri(d->plugin));
		if (!state)
			return -1;
		// Port values already include the default state and the score's explicit overrides.
		lilv_state_restore(state, l->instance, NULL, NULL, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, l->feature_ptrs);
		lilv_state_free(state);
	}
	reset(dsp);
	if (process(dsp, NULL, NULL, 0, NULL))
		return -1;
	restart(dsp);
	return 0;
}
