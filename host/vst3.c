#include "vst3.h"
#include "platform.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

enum { VST_ERROR = 1u << 31 };
typedef struct {
	void *library;
	Steinberg_IPluginFactory *factory;
	VstHost *host;
} VstModule;
static void close_module(void *data) {
	VstModule *m = data;
	if (!m)
		return;
	vst_release(m->factory);
	vst_release(m->host);
	vst_library_close(m->library);
	free(m);
}
static void *open_module(const char *path) {
	VstModule *m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;
	m->library = vst_library_open(path);
	Steinberg_IPluginFactory *(*factory)(void) = m->library ? library_symbol(m->library, "GetPluginFactory") : NULL;
	if (!factory || !(m->factory = factory()) || !(m->host = vst_host_new())) {
		close_module(m);
		return NULL;
	}
	Steinberg_IPluginFactory3 *f = vst_query(m->factory, Steinberg_IPluginFactory3_iid);
	if (f) {
		f->lpVtbl->setHostContext(f, vst_host_context(m->host));
		vst_release(f);
	}
	return m;
}
const ModuleOps vst3_module_ops = {open_module, close_module};
Steinberg_IPluginFactory *vst_factory(Module *module) {
	return ((VstModule *)module_data(module))->factory;
}
VstHost *vst_host(Module *module) {
	return ((VstModule *)module_data(module))->host;
}

static Vst *handler_owner(void *self) {
	return (Vst *)((char *)self - offsetof(Vst, handler));
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE handler_query(void *s, const Steinberg_TUID iid, void **out) {
	return vst_match(s, iid, Steinberg_Vst_IComponentHandler_iid, out);
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE gesture(void *s, Steinberg_Vst_ParamID id) {
	return vst_parameter(handler_owner(s), id) >= 0 ? Steinberg_kResultOk : Steinberg_kInvalidArgument;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE edit(void *s, Steinberg_Vst_ParamID id, double value) {
	Vst *v = handler_owner(s);
	int i = vst_parameter(v, id);
	if (i < 0 || !isfinite(value) || value < 0 || value > 1 ||
	    (v->info[i].flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsReadOnly))
		return Steinberg_kInvalidArgument;
	v->controller_values[i] = value;
	double plain = value * vst_scale(v->info + i);
	if (v->info[i].stepCount)
		plain = round(plain);
	if (v->dsp)
		edit_dsp(v->dsp, i, plain);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE request_restart(void *s, Steinberg_int32 flags) {
	atomic_fetch_or(&handler_owner(s)->requests, (unsigned)flags);
	return Steinberg_kResultOk;
}
static int parse_id(const char *text, Steinberg_TUID id) {
	if (!text || strlen(text) != 32)
		return -1;
	for (int i = 0; i < 16; ++i) {
		unsigned byte = 0;
		for (int j = 0; j < 2; ++j) {
			char c = text[i * 2 + j];
			int digit = c >= '0' && c <= '9' ? c - '0'
			    : c >= 'A' && c <= 'F'       ? c - 'A' + 10
			    : c >= 'a' && c <= 'f'       ? c - 'a' + 10
			                                 : -1;
			if (digit < 0)
				return -1;
			byte = byte * 16 + digit;
		}
		id[i] = byte;
	}
	return 0;
}
static void controller_state(Vst *v) {
	if (!v->controller)
		return;
	VstStream *b = vst_stream_new();
	if (b) {
		if (v->component->lpVtbl->getState(v->component, &b->api) == Steinberg_kResultOk && !b->failed) {
			b->position = 0;
			v->controller->lpVtbl->setComponentState(v->controller, &b->api);
		}
		vst_release(b);
	}
}
static void midi_mapping(Vst *v) {
	Steinberg_Vst_IMidiMapping *mapping = vst_query(v->controller, Steinberg_Vst_IMidiMapping_iid);
	for (int channel = 0; channel < 16; ++channel)
		for (int cc = 0; cc < 130; ++cc) {
			Steinberg_Vst_ParamID id;
			v->midi_map[channel][cc] = mapping && v->midi >= 0 &&
			        mapping->lpVtbl->getMidiControllerAssignment(mapping, v->midi, channel, cc, &id) ==
			            Steinberg_kResultOk
			    ? vst_parameter(v, id)
			    : -1;
		}
	vst_release(mapping);
	Steinberg_Vst_IUnitInfo *units = vst_query(v->controller, Steinberg_Vst_IUnitInfo_iid);
	for (int channel = 0; channel < 16; ++channel) {
		v->midi_map[channel][130] = -1;
		if (v->midi < 0)
			continue;
		Steinberg_Vst_UnitID unit = Steinberg_Vst_kRootUnitId;
		if (units &&
		    units->lpVtbl->getUnitByBus(units, Steinberg_Vst_MediaTypes_kEvent, Steinberg_Vst_BusDirections_kInput,
		        v->midi, channel, &unit) != Steinberg_kResultOk)
			unit = Steinberg_Vst_kRootUnitId;
		for (int i = 0; i < v->nparams; ++i)
			if (v->info[i].unitId == unit &&
			    (v->info[i].flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsProgramChange)) {
				v->midi_map[channel][130] = i;
				break;
			}
	}
	vst_release(units);
}
int vst_create(Vst *v, Module *module, const char *id) {
	static Steinberg_Vst_IComponentHandlerVtbl handler = {
	    handler_query, vst_borrow, vst_borrow, gesture, edit, gesture, request_restart};
	v->module = module;
	v->handler.lpVtbl = &handler;
	atomic_init(&v->requests, 0);
	if (parse_id(id, v->cid))
		return -1;
	Steinberg_IPluginFactory *f = vst_factory(module);
	if (f->lpVtbl->createInstance(f, v->cid, Steinberg_Vst_IComponent_iid, (void **)&v->component) !=
	        Steinberg_kResultOk ||
	    !v->component)
		return -1;
	v->component->lpVtbl->setIoMode(v->component, Steinberg_Vst_IoModes_kAdvanced);
	if (v->component->lpVtbl->initialize(v->component, vst_host_context(vst_host(module))) != Steinberg_kResultOk)
		return -1;
	v->initialized = 1;
	v->processor = vst_query(v->component, Steinberg_Vst_IAudioProcessor_iid);
	if (!v->processor ||
	    v->processor->lpVtbl->canProcessSampleSize(v->processor, Steinberg_Vst_SymbolicSampleSizes_kSample32) !=
	        Steinberg_kResultOk)
		return -1;
	v->controller = vst_query(v->component, Steinberg_Vst_IEditController_iid);
	if (!v->controller) {
		Steinberg_TUID cid;
		if (v->component->lpVtbl->getControllerClassId(v->component, cid) == Steinberg_kResultOk) {
			if (f->lpVtbl->createInstance(f, cid, Steinberg_Vst_IEditController_iid, (void **)&v->controller) !=
			        Steinberg_kResultOk ||
			    !v->controller ||
			    v->controller->lpVtbl->initialize(v->controller, vst_host_context(vst_host(module))) !=
			        Steinberg_kResultOk)
				return -1;
			v->controller_initialized = 1;
		}
	}
	if (v->controller) {
		if (v->controller_initialized) {
			v->connections[0] = vst_query(v->component, Steinberg_Vst_IConnectionPoint_iid);
			v->connections[1] = vst_query(v->controller, Steinberg_Vst_IConnectionPoint_iid);
			if (v->connections[0] && v->connections[1])
				for (int i = 0; i < 2; ++i) {
					Steinberg_Vst_IConnectionPoint *c = v->connections[i];
					if (c->lpVtbl->connect(c, v->connections[1 - i]) != Steinberg_kResultOk)
						return -1;
					v->connected[i] = 1;
				}
		}
		controller_state(v);
	}
	if (vst_parameters(v))
		return -1;
	if (v->controller)
		v->handler_set = v->controller->lpVtbl->setComponentHandler(v->controller, &v->handler) == Steinberg_kResultOk;
	return 0;
}
static void stop_processing(Vst *v) {
	if (v->processing) {
		v->processor->lpVtbl->setProcessing(v->processor, 0);
		v->processing = 0;
	}
}
static void suspend(Vst *v) {
	stop_processing(v);
	if (v->active) {
		v->component->lpVtbl->setActive(v->component, 0);
		v->active = 0;
	}
}
void vst_destroy(Vst *v) {
	suspend(v);
	if (v->handler_set)
		v->controller->lpVtbl->setComponentHandler(v->controller, NULL);
	for (int i = 0; i < 2; ++i) {
		Steinberg_Vst_IConnectionPoint *c = v->connections[i];
		if (v->connected[i])
			c->lpVtbl->disconnect(c, v->connections[1 - i]);
	}
	for (int i = 0; i < 2; ++i)
		vst_release(v->connections[i]);
	if (v->controller_initialized)
		v->controller->lpVtbl->terminate(v->controller);
	vst_release(v->controller);
	vst_release(v->processor);
	if (v->initialized)
		v->component->lpVtbl->terminate(v->component);
	vst_release(v->component);
	free(v->info);
	free(v->by_id);
	free(v->controller_values);
	free(v->inputs);
	free(v->outputs);
	free(v->input_channels);
	free(v->output_channels);
	free(v->silence);
	free(v->scratch);
	free(v->in.queues);
	free(v->in.points);
	free(v->in.order);
	free(v->out.queues);
	free(v->out.points);
	free(v->out.order);
}

static Steinberg_tresult SMTG_STDMETHODCALLTYPE queue_query(void *s, const Steinberg_TUID iid, void **out) {
	return vst_match(s, iid, Steinberg_Vst_IParamValueQueue_iid, out);
}
static Steinberg_Vst_ParamID SMTG_STDMETHODCALLTYPE queue_id(void *s) {
	return ((VstQueue *)s)->id;
}
static Steinberg_int32 SMTG_STDMETHODCALLTYPE point_count(void *s) {
	return ((VstQueue *)s)->count;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE point_get(
    void *s, Steinberg_int32 index, Steinberg_int32 *offset, double *value) {
	VstQueue *q = s;
	if (index < 0 || index >= q->count || !offset || !value)
		return Steinberg_kInvalidArgument;
	int point = q->head;
	while (index--)
		point = q->changes->points[point].next;
	*offset = q->changes->points[point].offset;
	*value = q->changes->points[point].value;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE point_add(
    void *s, Steinberg_int32 offset, double value, Steinberg_int32 *index) {
	VstQueue *q = s;
	VstChanges *c = q->changes;
	if (!index || offset < 0 || !isfinite(value) || value < 0 || value > 1 ||
	    (q->count && offset < c->points[q->tail].offset))
		return Steinberg_kInvalidArgument;
	if (q->count && offset == c->points[q->tail].offset) {
		c->points[q->tail].value = value;
		*index = q->count - 1;
		return Steinberg_kResultOk;
	}
	if (c->used == c->capacity) {
		atomic_fetch_or(&c->owner->requests, VST_ERROR);
		return Steinberg_kOutOfMemory;
	}
	int point = c->used++;
	c->points[point] = (VstPoint){value, offset, -1};
	if (q->count)
		c->points[q->tail].next = point;
	else
		q->head = point;
	q->tail = point;
	*index = q->count++;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE changes_query(void *s, const Steinberg_TUID iid, void **out) {
	return vst_match(s, iid, Steinberg_Vst_IParameterChanges_iid, out);
}
static Steinberg_int32 SMTG_STDMETHODCALLTYPE changes_count(void *s) {
	return ((VstChanges *)s)->count;
}
static Steinberg_Vst_IParamValueQueue *SMTG_STDMETHODCALLTYPE changes_get(void *s, Steinberg_int32 index) {
	VstChanges *c = s;
	return index >= 0 && index < c->count ? &c->queues[c->order[index]].api : NULL;
}
static Steinberg_Vst_IParamValueQueue *SMTG_STDMETHODCALLTYPE changes_add(
    void *s, const Steinberg_Vst_ParamID *id, Steinberg_int32 *index) {
	VstChanges *c = s;
	int i = id ? vst_parameter(c->owner, *id) : -1;
	if (i < 0 || !index)
		return NULL;
	VstQueue *q = c->queues + i;
	if (q->slot < 0) {
		q->slot = c->count;
		c->order[c->count++] = i;
	}
	*index = q->slot;
	return &c->queues[i].api;
}
static int changes_init(VstChanges *c, Vst *v) {
	static Steinberg_Vst_IParameterChangesVtbl changes = {
	    changes_query, vst_borrow, vst_borrow, changes_count, changes_get, changes_add};
	static Steinberg_Vst_IParamValueQueueVtbl queue = {
	    queue_query, vst_borrow, vst_borrow, queue_id, point_count, point_get, point_add};
	c->api.lpVtbl = &changes;
	c->owner = v;
	c->capacity = v->nparams + 4096;
	c->queues = calloc(v->nparams, sizeof(*c->queues));
	c->order = calloc(v->nparams, sizeof(*c->order));
	c->points = calloc(c->capacity, sizeof(*c->points));
	if ((v->nparams && (!c->queues || !c->order)) || !c->points)
		return -1;
	for (int i = 0; i < v->nparams; ++i)
		c->queues[i] = (VstQueue){.api = {&queue}, .id = v->info[i].id, .changes = c, .slot = -1};
	return 0;
}
static void changes_clear(VstChanges *c) {
	for (int i = 0; i < c->count; ++i) {
		c->queues[c->order[i]].count = 0;
		c->queues[c->order[i]].slot = -1;
	}
	c->count = c->used = 0;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE events_query(void *s, const Steinberg_TUID iid, void **out) {
	return vst_match(s, iid, Steinberg_Vst_IEventList_iid, out);
}
static Steinberg_int32 SMTG_STDMETHODCALLTYPE events_count(void *s) {
	return ((VstEvents *)s)->count;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE events_get(
    void *s, Steinberg_int32 index, struct Steinberg_Vst_Event *event) {
	VstEvents *e = s;
	if (index < 0 || index >= e->count || !event)
		return Steinberg_kInvalidArgument;
	*event = e->events[index];
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE events_add(void *s, struct Steinberg_Vst_Event *event) {
	VstEvents *list = s;
	DSP *dsp = list->output;
	if (!dsp || !event || event->busIndex != 0 || event->sampleOffset < 0)
		return Steinberg_kInvalidArgument;
	uint8_t bytes[3];
	int channel, pitch, status;
	double value;
	switch (event->type) {
	case Steinberg_Vst_Event_EventTypes_kNoteOnEvent:
		channel = event->Steinberg_Vst_Event_noteOn.channel;
		pitch = event->Steinberg_Vst_Event_noteOn.pitch;
		value = event->Steinberg_Vst_Event_noteOn.velocity;
		status = 0x90;
		break;
	case Steinberg_Vst_Event_EventTypes_kNoteOffEvent:
		channel = event->Steinberg_Vst_Event_noteOff.channel;
		pitch = event->Steinberg_Vst_Event_noteOff.pitch;
		value = event->Steinberg_Vst_Event_noteOff.velocity;
		status = 0x80;
		break;
	case Steinberg_Vst_Event_EventTypes_kPolyPressureEvent:
		channel = event->Steinberg_Vst_Event_polyPressure.channel;
		pitch = event->Steinberg_Vst_Event_polyPressure.pitch;
		value = event->Steinberg_Vst_Event_polyPressure.pressure;
		status = 0xa0;
		break;
	case Steinberg_Vst_Event_EventTypes_kDataEvent: {
		const struct Steinberg_Vst_DataEvent *e = &event->Steinberg_Vst_Event_data;
		return e->type == Steinberg_Vst_DataEvent_DataTypes_kMidiSysEx &&
		        !dsp_midi_output(dsp, event->sampleOffset, e->bytes, e->size)
		    ? Steinberg_kResultOk
		    : Steinberg_kInvalidArgument;
	}
	case Steinberg_Vst_Event_EventTypes_kLegacyMIDICCOutEvent: {
		const struct Steinberg_Vst_LegacyMIDICCOutEvent *e = &event->Steinberg_Vst_Event_midiCCOut;
		if (e->channel < 0 || e->channel > 15 || e->value < 0)
			return Steinberg_kInvalidArgument;
		bytes[0] = 0xb0 | e->channel;
		bytes[1] = e->controlNumber;
		bytes[2] = e->value;
		if (e->controlNumber >= 128) {
			int type = e->controlNumber == 128 ? 0xd0
			    : e->controlNumber == 129      ? 0xe0
			    : e->controlNumber == 130      ? 0xc0
			    : e->controlNumber == 131      ? 0xa0
			                                   : 0;
			if (!type)
				return Steinberg_kNotImplemented;
			bytes[0] = type | e->channel;
			bytes[1] = e->value;
			bytes[2] = e->value2;
		}
		return dsp_midi_output(dsp, event->sampleOffset, bytes, midi_size(bytes[0])) ? Steinberg_kInvalidArgument
		                                                                             : Steinberg_kResultOk;
	}
	default:
		return Steinberg_kNotImplemented;
	}
	if (channel < 0 || channel > 15 || pitch < 0 || pitch > 127 || !isfinite(value) || value < 0 || value > 1)
		return Steinberg_kInvalidArgument;
	bytes[0] = status | channel;
	bytes[1] = pitch;
	bytes[2] = lround(value * 127);
	if (status == 0x90 && !bytes[2])
		bytes[2] = 1;
	return dsp_midi_output(dsp, event->sampleOffset, bytes, 3) ? Steinberg_kInvalidArgument : Steinberg_kResultOk;
}
static void set(DSP *dsp, size_t index, double value) {
	Vst *v = dsp->instance;
	Steinberg_int32 slot;
	Steinberg_Vst_IParamValueQueue *q = changes_add(&v->in, &v->info[index].id, &slot);
	if (!q || q->lpVtbl->addPoint(q, 0, value / vst_scale(v->info + index), &slot) != Steinberg_kResultOk)
		atomic_fetch_or(&v->requests, VST_ERROR);
}
static int start_processing(Vst *v) {
	if (!v->active)
		return -1;
	int result = v->processor->lpVtbl->setProcessing(v->processor, 1);
	// The SDK's default AudioEffect returns kNotImplemented for this notification.
	if (result != Steinberg_kResultOk && result != Steinberg_kNotImplemented)
		return -1;
	v->processing = 1;
	return 0;
}
static void reset(DSP *dsp) {
	Vst *v = dsp->instance;
	v->events.count = 0;
	midi_clear(&v->sysex);
	stop_processing(v);
	if (start_processing(v))
		atomic_fetch_or(&v->requests, VST_ERROR);
}
static void midi(DSP *dsp, size_t bus, const uint8_t *m, size_t size) {
	Vst *v = dsp->instance;
	if ((int)bus != v->midi)
		return;
	int status = m[0] & 0xf0, channel = m[0] & 15;
	int cc = status == 0xb0 ? m[1] : status == 0xd0 ? 128 : status == 0xe0 ? 129 : status == 0xc0 ? 130 : -1;
	if (cc >= 0) {
		int i = v->midi_map[channel][cc];
		if (i >= 0 && !(dsp->config.parameters[i].flags & PARAM_OUTPUT)) {
			double value = status == 0xc0 ? fmin(m[1], vst_scale(v->info + i)) / vst_scale(v->info + i)
			    : status == 0xe0          ? (m[1] + 128 * m[2]) / 16383.0
			                              : (status == 0xd0 ? m[1] : m[2]) / 127.0;
			value *= vst_scale(v->info + i);
			set_dsp(dsp, i, v->info[i].stepCount ? round(value) : value);
		}
		return;
	}
	if (m[0] != 0xf0 && status != 0x80 && status != 0x90 && status != 0xa0)
		return;
	if (v->events.count == 256) {
		atomic_fetch_or(&v->requests, VST_ERROR);
		return;
	}
	struct Steinberg_Vst_Event *e = v->events.events + v->events.count++;
	*e = (struct Steinberg_Vst_Event){.busIndex = bus};
	if (m[0] == 0xf0) {
		unsigned offset = v->sysex.used;
		if (midi_add(&v->sysex, 0, m, size)) {
			atomic_fetch_or(&v->requests, VST_ERROR);
			return;
		}
		e->type = Steinberg_Vst_Event_EventTypes_kDataEvent;
		e->Steinberg_Vst_Event_data = (struct Steinberg_Vst_DataEvent){
		    size, Steinberg_Vst_DataEvent_DataTypes_kMidiSysEx, v->sysex.data + offset};
	} else if (status == 0x90 && m[2]) {
		e->type = Steinberg_Vst_Event_EventTypes_kNoteOnEvent;
		e->Steinberg_Vst_Event_noteOn = (struct Steinberg_Vst_NoteOnEvent){
		    .channel = channel, .pitch = m[1], .velocity = m[2] / 127.f, .noteId = -1};
	} else if (status == 0xa0) {
		e->type = Steinberg_Vst_Event_EventTypes_kPolyPressureEvent;
		e->Steinberg_Vst_Event_polyPressure = (struct Steinberg_Vst_PolyPressureEvent){
		    .channel = channel, .pitch = m[1], .pressure = m[2] / 127.f, .noteId = -1};
	} else {
		e->type = Steinberg_Vst_Event_EventTypes_kNoteOffEvent;
		e->Steinberg_Vst_Event_noteOff = (struct Steinberg_Vst_NoteOffEvent){
		    .channel = channel, .pitch = m[1], .velocity = m[2] / 127.f, .noteId = -1};
	}
}
static int process(DSP *dsp, const float **inputs, float **outputs, size_t frames, const ProcessTime *time) {
	Vst *v = dsp->instance;
	if (!v->active || frames > v->block || (atomic_load(&v->requests) & VST_ERROR))
		return -1;
	if (!v->processing && start_processing(v))
		return -1;
	for (int i = 0; i < v->input_slots; ++i)
		v->input_channels[i] = inputs && inputs[i] ? (float *)inputs[i] : v->silence;
	for (int i = 0; i < v->output_slots; ++i)
		v->output_channels[i] = outputs && i < dsp->config.output ? outputs[i] : v->scratch + i * v->block;
	for (int i = 0; i < v->ninputs; ++i)
		v->inputs[i].silenceFlags = 0;
	for (int i = 0; i < v->noutputs; ++i)
		v->outputs[i].silenceFlags = 0;
	struct Steinberg_Vst_ProcessContext context = {.sampleRate = v->rate};
	if (time) {
		context.state = Steinberg_Vst_ProcessContext_StatesAndFlags_kProjectTimeMusicValid |
		    Steinberg_Vst_ProcessContext_StatesAndFlags_kTempoValid;
		if (time->playing)
			context.state |= Steinberg_Vst_ProcessContext_StatesAndFlags_kPlaying;
		context.projectTimeSamples = time->frame;
		context.projectTimeMusic = time->beat;
		context.tempo = time->bpm > 0 ? time->bpm : 120;
	}
	for (int i = 0; i < v->events.count; ++i)
		v->events.events[i].ppqPosition = context.projectTimeMusic;
	struct Steinberg_Vst_ProcessData block = {.processMode = v->mode,
	    .symbolicSampleSize = Steinberg_Vst_SymbolicSampleSizes_kSample32,
	    .numSamples = frames,
	    .numInputs = v->ninputs,
	    .numOutputs = v->noutputs,
	    .inputs = v->inputs,
	    .outputs = v->outputs,
	    .inputParameterChanges = &v->in.api,
	    .outputParameterChanges = &v->out.api,
	    .inputEvents = &v->events.api,
	    .outputEvents = &v->output_events.api,
	    .processContext = &context};
	int result = v->processor->lpVtbl->process(v->processor, &block);
	for (int i = 0; i < v->out.count; ++i) {
		int index = v->out.order[i];
		VstQueue *q = v->out.queues + index;
		if (q->count) {
			double value = v->out.points[q->tail].value * vst_scale(v->info + index);
			atomic_store(&dsp->values[index], v->info[index].stepCount ? round(value) : value);
		}
	}
	// A plugin may mark silence without writing the corresponding channel buffers.
	if (frames && outputs)
		for (int i = 0; i < dsp->config.output; ++i)
			if (v->outputs[0].silenceFlags & ((uint64_t)1 << i))
				memset(outputs[i], 0, frames * sizeof(float));
	changes_clear(&v->in);
	changes_clear(&v->out);
	v->events.count = 0;
	midi_clear(&v->sysex);
	return result != Steinberg_kResultOk || (atomic_load(&v->requests) & VST_ERROR) ? -1 : 0;
}
static void idle(DSP *dsp, const ProcessTime *time) {
	Vst *v = dsp->instance;
	if (!v->in.count)
		return;
	// A seek may have queued notes. A zero-frame parameter flush must not consume them.
	int notes = v->events.count;
	unsigned used = v->sysex.used, count = v->sysex.count;
	v->events.count = 0;
	if (process(dsp, NULL, NULL, 0, time))
		atomic_fetch_or(&v->requests, VST_ERROR);
	v->events.count = notes;
	v->sysex.used = used;
	v->sysex.count = count;
}
void vst_update_controller(Vst *v) {
	DSP *dsp = v->dsp;
	for (int i = 0; i < v->nparams; ++i) {
		const struct Steinberg_Vst_ParameterInfo *p = v->info + i;
		double value;
		if (read_dsp(dsp, i, &value)) {
			value /= vst_scale(p);
			if (value != v->controller_values[i]) {
				v->controller->lpVtbl->setParamNormalized(v->controller, p->id, value);
				v->controller_values[i] = value;
			}
		}
	}
}
static int poll_instance(DSP *dsp) {
	Vst *v = dsp->instance;
	vst_host_poll(vst_host(v->module));
	unsigned flags = atomic_fetch_and(&v->requests, ~Steinberg_Vst_RestartFlags_kParamValuesChanged);
	for (int i = 0; i < v->nparams; ++i) {
		const struct Steinberg_Vst_ParameterInfo *p = v->info + i;
		if ((flags & Steinberg_Vst_RestartFlags_kParamValuesChanged) &&
		    !(p->flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsReadOnly)) {
			double value = v->controller->lpVtbl->getParamNormalized(v->controller, p->id);
			if (atomic_load(&dsp->requested[i]) == atomic_load(&dsp->applied[i]))
				edit(&v->handler, p->id, value);
		}
	}
	vst_update_controller(v);
	flags = atomic_load(&v->requests);
	return flags & VST_ERROR ? -1 : !!flags;
}
static int compatible(Vst *v) {
	const PluginConfig *c = &v->dsp->config;
	int input, output, midi;
	if (vst_layout(v, &input, &output, &midi) || input != c->input || output != c->output || midi != c->midi ||
	    v->midi_output != c->midi_output || v->input_slots != c->inputs || v->nparams != c->nparams ||
	    (v->controller && v->controller->lpVtbl->getParameterCount(v->controller) != v->nparams))
		return -1;
	for (int i = 0; i < v->nparams; ++i) {
		struct Steinberg_Vst_ParameterInfo p;
		if (v->controller->lpVtbl->getParameterInfo(v->controller, i, &p) != Steinberg_kResultOk)
			return -1;
		unsigned flags = (p.flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsReadOnly ? PARAM_OUTPUT : 0) |
		    (p.flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kCanAutomate ? PARAM_AUTOMATABLE : 0) |
		    (p.stepCount ? PARAM_INTEGER : 0);
		if (p.id != c->parameters[i].id || p.stepCount != v->info[i].stepCount || flags != c->parameters[i].flags ||
		    c->parameters[i].minimum != 0 || c->parameters[i].maximum != vst_scale(&p))
			return -1;
		v->info[i] = p;
	}
	return 0;
}
static int activate(Vst *v) {
	struct Steinberg_Vst_ProcessSetup setup = {v->mode, Steinberg_Vst_SymbolicSampleSizes_kSample32, v->block, v->rate};
	if (v->processor->lpVtbl->setupProcessing(v->processor, &setup) != Steinberg_kResultOk)
		return -1;
	for (int dir = 0; dir < 2; ++dir)
		for (int i = 0; i < (dir ? v->noutputs : v->ninputs); ++i) {
			int connected = !i && (dir ? v->dsp->config.output : v->dsp->config.input);
			if (v->component->lpVtbl->activateBus(v->component, Steinberg_Vst_MediaTypes_kAudio, dir, i, connected) !=
			    Steinberg_kResultOk)
				return -1;
		}
	if (v->midi >= 0 &&
	    v->component->lpVtbl->activateBus(v->component, Steinberg_Vst_MediaTypes_kEvent,
	        Steinberg_Vst_BusDirections_kInput, v->midi, 1) != Steinberg_kResultOk)
		return -1;
	if (v->midi_output &&
	    v->component->lpVtbl->activateBus(v->component, Steinberg_Vst_MediaTypes_kEvent,
	        Steinberg_Vst_BusDirections_kOutput, 0, 1) != Steinberg_kResultOk)
		return -1;
	midi_mapping(v);
	if (v->component->lpVtbl->setActive(v->component, 1) != Steinberg_kResultOk)
		return -1;
	v->active = 1;
	return 0;
}
static int offline(DSP *dsp, int enabled) {
	Vst *v = dsp->instance;
	int mode = enabled ? Steinberg_Vst_ProcessModes_kOffline : Steinberg_Vst_ProcessModes_kRealtime;
	if (atomic_load(&v->requests) & VST_ERROR)
		return -1;
	if (v->mode == mode)
		return 0;
	idle(dsp, NULL);
	suspend(v);
	int previous = v->mode;
	v->mode = mode;
	if (!activate(v))
		return 0;
	v->mode = previous;
	if (activate(v))
		atomic_fetch_or(&v->requests, VST_ERROR);
	return -1;
}
static int restart(DSP *dsp) {
	Vst *v = dsp->instance;
	// Consume this batch only. Callbacks during reconfiguration may queue another one.
	unsigned flags = atomic_fetch_and(&v->requests, VST_ERROR | Steinberg_Vst_RestartFlags_kParamValuesChanged);
	flags &= ~Steinberg_Vst_RestartFlags_kParamValuesChanged;
	if (!flags)
		return 0;
	if (flags &
	    (VST_ERROR | Steinberg_Vst_RestartFlags_kReloadComponent | Steinberg_Vst_RestartFlags_kParamIDMappingChanged))
		goto fail;
	int reactivate = flags &
	    (Steinberg_Vst_RestartFlags_kIoChanged | Steinberg_Vst_RestartFlags_kLatencyChanged |
	        Steinberg_Vst_RestartFlags_kPrefetchableSupportChanged);
	if (reactivate) {
		idle(dsp, NULL);
		suspend(v);
	}
	if ((atomic_load(&v->requests) & VST_ERROR) || (reactivate && vst_arrange(v)) || compatible(v) ||
	    (reactivate && activate(v)))
		goto fail;
	// Mapping and metadata updates must preserve voices and effect tails.
	if (!reactivate && (flags & Steinberg_Vst_RestartFlags_kMidiCCAssignmentChanged))
		midi_mapping(v);
	return 0;
fail:
	fputs("[VST3] Plugin configuration changed; prepare the score again\n", stderr);
	atomic_fetch_or(&v->requests, VST_ERROR);
	return -1;
}
static uint32_t latency(DSP *dsp) {
	Vst *v = dsp->instance;
	return v->processor->lpVtbl->getLatencySamples(v->processor);
}
static int state(DSP *dsp, State *buffer, int write) {
	Vst *v = dsp->instance;
	if (atomic_load(&v->requests) & VST_ERROR)
		return -1;
	sync_dsp(dsp, NULL);
	idle(dsp, NULL);
	vst_update_controller(v);
	suspend(v);
	VstStream *parts[2] = {vst_stream_new(), vst_stream_new()};
	int ok = parts[0] && parts[1];
	unsigned char header[32] = {'T', 'B', 'V', '3', 1};
	memcpy(header + 8, v->cid, 16);
	if (ok && write) {
		ok = v->component->lpVtbl->getState(v->component, &parts[0]->api) == Steinberg_kResultOk;
		if (v->controller) {
			int result = v->controller->lpVtbl->getState(v->controller, &parts[1]->api);
			if (result == Steinberg_kNotImplemented)
				parts[1]->length = 0; // Controller state is optional; a failed save is not.
			else
				ok &= result == Steinberg_kResultOk;
		}
		for (int i = 0; i < 2; ++i) {
			ok &= !parts[i]->failed;
			for (int b = 0; b < 4; ++b)
				header[24 + i * 4 + b] = parts[i]->length >> (8 * b);
		}
		ok = ok && !state_write(buffer, header, sizeof(header));
		for (int i = 0; ok && i < 2; ++i)
			ok = !parts[i]->length || !state_write(buffer, parts[i]->data, parts[i]->length);
	} else if (ok) {
		unsigned char read[32];
		ok = state_read(buffer, read, sizeof(read)) == sizeof(read) && !memcmp(read, header, 24);
		for (int i = 0; ok && i < 2; ++i) {
			uint32_t size = 0;
			for (int b = 0; b < 4; ++b)
				size |= (uint32_t)read[24 + i * 4 + b] << (8 * b);
			ok = size <= STATE_LIMIT;
			if (ok && size) {
				parts[i]->data = malloc(size);
				parts[i]->capacity = parts[i]->length = size;
				ok = parts[i]->data && state_read(buffer, parts[i]->data, size) == size;
			}
		}
		ok = ok && buffer->position == buffer->length;
		if (ok)
			ok = v->component->lpVtbl->setState(v->component, &parts[0]->api) == Steinberg_kResultOk;
		if (ok && v->controller) {
			parts[0]->position = 0;
			v->controller->lpVtbl->setComponentState(v->controller, &parts[0]->api);
			if (parts[1]->length)
				ok = v->controller->lpVtbl->setState(v->controller, &parts[1]->api) == Steinberg_kResultOk;
		}
	}
	for (int i = 0; i < 2; ++i)
		vst_release(parts[i]);
	atomic_fetch_and(&v->requests,
	    VST_ERROR | Steinberg_Vst_RestartFlags_kParamValuesChanged | Steinberg_Vst_RestartFlags_kReloadComponent |
	        Steinberg_Vst_RestartFlags_kParamIDMappingChanged);
	if ((atomic_load(&v->requests) & VST_ERROR) || vst_arrange(v) || compatible(v) || activate(v)) {
		ok = 0;
		atomic_fetch_or(&v->requests, VST_ERROR);
	}
	if (!write && v->controller)
		atomic_fetch_or(&v->requests, Steinberg_Vst_RestartFlags_kParamValuesChanged);
	return ok ? 0 : -1;
}
static void close_instance(DSP *dsp) {
	Vst *v = dsp->instance;
	if (v) {
		vst_destroy(v);
		free(v);
	}
}
static const DspOps ops = {.close = close_instance,
    .set = set,
    .reset = reset,
    .midi = midi,
    .process = process,
    .idle = idle,
    .poll = poll_instance,
    .restart = restart,
    .offline = offline,
    .latency = latency,
    .state = state};
int vst3_open(DSP *dsp, unsigned rate, size_t capacity) {
	static Steinberg_Vst_IEventListVtbl events = {
	    events_query, vst_borrow, vst_borrow, events_count, events_get, events_add};
	Vst *v = calloc(1, sizeof(*v));
	if (!v)
		return -1;
	dsp->instance = v;
	dsp->ops = &ops;
	v->dsp = dsp;
	if (vst_create(v, dsp->module, dsp->config.id))
		return -1;
	v->rate = rate;
	v->block = capacity;
	v->events.api.lpVtbl = v->output_events.api.lpVtbl = &events;
	v->output_events.output = dsp;
	if (vst_arrange(v) || compatible(v) || changes_init(&v->in, v) || changes_init(&v->out, v))
		return -1;
	v->inputs = calloc(v->ninputs, sizeof(*v->inputs));
	v->outputs = calloc(v->noutputs, sizeof(*v->outputs));
	v->input_channels = calloc(v->input_slots, sizeof(float *));
	v->output_channels = calloc(v->output_slots, sizeof(float *));
	v->silence = calloc(capacity, sizeof(float));
	v->scratch = calloc(v->output_slots * capacity, sizeof(float));
	if ((v->ninputs && !v->inputs) || (v->noutputs && !v->outputs) || (v->input_slots && !v->input_channels) ||
	    (v->output_slots && !v->output_channels) || !v->silence || (v->output_slots && !v->scratch))
		return -1;
	for (int dir = 0; dir < 2; ++dir) {
		int offset = 0;
		for (int i = 0; i < (dir ? v->noutputs : v->ninputs); ++i) {
			struct Steinberg_Vst_BusInfo b;
			if (v->component->lpVtbl->getBusInfo(v->component, Steinberg_Vst_MediaTypes_kAudio, dir, i, &b) !=
			    Steinberg_kResultOk)
				return -1;
			(dir ? v->outputs : v->inputs)[i] = (struct Steinberg_Vst_AudioBusBuffers){.numChannels = b.channelCount,
			    .Steinberg_Vst_AudioBusBuffers_channelBuffers32 =
			        (dir ? v->output_channels : v->input_channels) + offset};
			offset += b.channelCount;
		}
	}
	atomic_store(&v->requests, 0);
	if (activate(v))
		return -1;
	for (int i = 0; i < v->nparams; ++i)
		if (!(dsp->config.parameters[i].flags & PARAM_OUTPUT))
			set(dsp, i, dsp->config.parameters[i].value);
	reset(dsp);
	idle(dsp, NULL);
	return atomic_load(&v->requests) & VST_ERROR ? -1 : 0;
}
