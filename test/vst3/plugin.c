// Independent C VST3 fixture: separate synth/controller and a combined effect/controller.
#include <uchar.h>
#include "host/vst3_api.h"
#include <X11/Xlib.h>
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	void *table, *owner;
} Interface;
typedef struct Instance Instance;
typedef struct View View;
struct Instance {
	Interface component, processor, controller, connection, mapping;
	unsigned refs;
	int effect, control_only, initialized, active, processing, note, cursor;
	int arranged, mode;
	Steinberg_FUnknown *host;
	Steinberg_Vst_IComponentHandler *handler;
	Steinberg_Vst_IConnectionPoint *peer;
	double values[4], private_value;
	float delay[7][2];
	View *view;
};
struct View {
	Interface view, event, timer;
	unsigned refs;
	Instance *owner;
	Steinberg_IPlugFrame *frame;
	Steinberg_Linux_IRunLoop *loop;
	Display *display;
	Window child;
	unsigned ticks;
	struct Steinberg_ViewRect rect;
};
static unsigned instances, entries, factory_refs;
static Steinberg_FUnknown *factory_host;
static const Steinberg_TUID synth_id = SMTG_INLINE_UID(0xABCDEF01, 0x22223333, 0x44445555, 0x66667777);
static const Steinberg_TUID effect_id = SMTG_INLINE_UID(0xABCDEF02, 0x22223333, 0x44445555, 0x66667777);
static const Steinberg_TUID controller_id = SMTG_INLINE_UID(0xABCDEF03, 0x22223333, 0x44445555, 0x66667777);
static const uint32_t ids[] = {987654321, 7, 42, 99};
static int equal(const char *a, const char *b) {
	return !memcmp(a, b, 16);
}
static void retain(void *object) {
	if (object) {
		Steinberg_FUnknown *p = object;
		p->lpVtbl->addRef(p);
	}
}
static void release(void *object) {
	if (object) {
		Steinberg_FUnknown *p = object;
		p->lpVtbl->release(p);
	}
}
static void *query(void *object, const char *id) {
	Steinberg_FUnknown *p = object;
	void *out = NULL;
	if (p)
		p->lpVtbl->queryInterface(p, id, &out);
	return out;
}
static void text(Steinberg_char16 *out, const char *in) {
	for (size_t i = 0; i < 128; ++i) {
		out[i] = in[i];
		if (!in[i])
			break;
	}
}
static int parameter(uint32_t id) {
	for (int i = 0; i < 4; ++i)
		if (ids[i] == id)
			return i;
	return -1;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE instance_ref(void *self) {
	return ++((Instance *)((Interface *)self)->owner)->refs;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE instance_release(void *self) {
	Instance *p = ((Interface *)self)->owner;
	if (--p->refs)
		return p->refs;
	assert(!p->initialized && !p->active && !p->processing && !p->view && !p->peer && !p->handler);
	--instances;
	free(p);
	return 0;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE instance_query(void *self, const Steinberg_TUID id, void **out) {
	Instance *p = ((Interface *)self)->owner;
	*out = NULL;
	if (equal(id, Steinberg_FUnknown_iid) || equal(id, Steinberg_IPluginBase_iid))
		*out = p->control_only ? &p->controller : &p->component;
	else if (equal(id, Steinberg_Vst_IComponent_iid) && !p->control_only)
		*out = &p->component;
	else if (equal(id, Steinberg_Vst_IAudioProcessor_iid) && !p->control_only)
		*out = &p->processor;
	else if (equal(id, Steinberg_Vst_IEditController_iid) && (p->effect || p->control_only))
		*out = &p->controller;
	else if (equal(id, Steinberg_Vst_IConnectionPoint_iid))
		*out = &p->connection;
	else if (equal(id, Steinberg_Vst_IMidiMapping_iid) && p->control_only)
		*out = &p->mapping;
	if (!*out)
		return Steinberg_kNoInterface;
	instance_ref(*out);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE initialize(void *self, Steinberg_FUnknown *host) {
	Instance *p = ((Interface *)self)->owner;
	assert(!p->initialized && host && factory_host);
	if (p->control_only && getenv("VST3_TEST_FAIL_INIT"))
		return Steinberg_kResultFalse;
	p->initialized = 1;
	p->host = host;
	retain(host);
	Steinberg_Vst_IHostApplication *h = query(host, Steinberg_Vst_IHostApplication_iid);
	assert(h);
	Steinberg_Vst_String128 name;
	assert(h->lpVtbl->getName(h, name) == Steinberg_kResultOk && name[0]);
	release(h);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE terminate(void *self) {
	Instance *p = ((Interface *)self)->owner;
	assert(p->initialized && !p->active && !p->processing && !p->view && !p->peer);
	p->initialized = 0;
	release(p->host);
	p->host = NULL;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE controller_class(void *self, Steinberg_TUID id) {
	(void)self;
	memcpy(id, controller_id, 16);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE io_mode(void *self, Steinberg_Vst_IoMode mode) {
	(void)self;
	(void)mode;
	return Steinberg_kResultOk;
}
static Steinberg_int32 SMTG_STDMETHODCALLTYPE bus_count(
    void *self, Steinberg_Vst_MediaType type, Steinberg_Vst_BusDirection dir) {
	Instance *p = ((Interface *)self)->owner;
	return type == 0 ? (dir == 1 || p->effect ? 2 : 1) : !p->effect;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE bus_info(void *self, Steinberg_Vst_MediaType type,
    Steinberg_Vst_BusDirection dir, Steinberg_int32 index, struct Steinberg_Vst_BusInfo *b) {
	Instance *p = ((Interface *)self)->owner;
	if (index < 0 || index >= bus_count(self, type, dir))
		return Steinberg_kInvalidArgument;
	*b = (struct Steinberg_Vst_BusInfo){.mediaType = type,
	    .direction = dir,
	    .channelCount = type ? 16 : 2,
	    .busType = !index && (type || dir || p->effect) ? Steinberg_Vst_BusTypes_kMain : Steinberg_Vst_BusTypes_kAux,
	    .flags = 1};
	text(b->name, type ? "Notes" : "Audio");
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE routing(
    void *s, struct Steinberg_Vst_RoutingInfo *a, struct Steinberg_Vst_RoutingInfo *b) {
	(void)s;
	(void)a;
	(void)b;
	return Steinberg_kNotImplemented;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE bus_active(void *s, Steinberg_Vst_MediaType type,
    Steinberg_Vst_BusDirection dir, Steinberg_int32 index, Steinberg_TBool active) {
	Instance *p = ((Interface *)s)->owner;
	assert(index >= 0 && index < bus_count(s, type, dir));
	assert(active == (!index && (type || dir || p->effect)));
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE active(void *self, Steinberg_TBool state) {
	Instance *p = ((Interface *)self)->owner;
	assert(p->initialized && !p->processing);
	p->active = state;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE save(void *self, Steinberg_IBStream *stream) {
	Instance *p = ((Interface *)self)->owner;
	assert(!p->processing);
	if (getenv("VST3_TEST_FAIL_SAVE") || (p->control_only && getenv("VST3_TEST_FAIL_CONTROLLER_SAVE")))
		return Steinberg_kResultFalse;
	if (p->control_only && getenv("VST3_TEST_NO_CONTROLLER_STATE"))
		return Steinberg_kNotImplemented;
	double data[5];
	memcpy(data, p->values, sizeof(p->values));
	data[4] = p->private_value;
	Steinberg_int32 n = 0;
	assert(stream->lpVtbl->write(stream, data, sizeof(data), &n) == Steinberg_kResultOk && n == sizeof(data));
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE load(void *self, Steinberg_IBStream *stream) {
	Instance *p = ((Interface *)self)->owner;
	assert(!p->processing);
	double data[5];
	Steinberg_int32 n = 0;
	if (stream->lpVtbl->read(stream, data, sizeof(data), &n) != Steinberg_kResultOk || n != sizeof(data))
		return Steinberg_kResultFalse;
	memcpy(p->values, data, sizeof(p->values));
	p->private_value = data[4];
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE arrangements(void *s, Steinberg_Vst_SpeakerArrangement *in,
    Steinberg_int32 ni, Steinberg_Vst_SpeakerArrangement *out, Steinberg_int32 no) {
	Instance *p = ((Interface *)s)->owner;
	assert(p->initialized && !p->active && !p->processing);
	assert(ni == bus_count(s, 0, 0) && no == bus_count(s, 0, 1));
	for (int i = 0; i < ni; ++i)
		assert(in[i] == 3);
	for (int i = 0; i < no; ++i)
		assert(out[i] == 3);
	p->arranged = 1;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE arrangement(
    void *s, Steinberg_Vst_BusDirection dir, Steinberg_int32 i, Steinberg_Vst_SpeakerArrangement *out) {
	assert(i >= 0 && i < bus_count(s, 0, dir));
	*out = 3;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE sample_size(void *s, Steinberg_int32 size) {
	(void)s;
	return size == Steinberg_Vst_SymbolicSampleSizes_kSample32 ? Steinberg_kResultOk : Steinberg_kResultFalse;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE latency(void *self) {
	Instance *p = ((Interface *)self)->owner;
	return p->values[2] >= .5 ? 7 : 0;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE setup(void *self, struct Steinberg_Vst_ProcessSetup *s) {
	Instance *p = ((Interface *)self)->owner;
	assert(p->initialized && p->arranged && !p->active && !p->processing);
	assert(s->sampleRate > 0 && s->maxSamplesPerBlock > 0 && s->symbolicSampleSize == 0);
	assert(s->processMode == Steinberg_Vst_ProcessModes_kRealtime ||
	    s->processMode == Steinberg_Vst_ProcessModes_kOffline);
	if (p->effect && s->processMode == Steinberg_Vst_ProcessModes_kOffline && getenv("VST3_TEST_FAIL_OFFLINE"))
		return Steinberg_kResultFalse;
	p->mode = s->processMode;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE processing(void *self, Steinberg_TBool state) {
	Instance *p = ((Interface *)self)->owner;
	assert(p->active);
	p->processing = state;
	if (state) {
		p->note = 0;
		p->cursor = 0;
		memset(p->delay, 0, sizeof(p->delay));
	}
	return p->effect ? Steinberg_kNotImplemented : Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE process(void *self, struct Steinberg_Vst_ProcessData *b) {
	Instance *p = ((Interface *)self)->owner;
	assert(p->active && p->processing && b->processContext && b->processContext->sampleRate > 0);
	assert(b->symbolicSampleSize == 0 && b->numOutputs == 2 && b->numInputs == (p->effect ? 2 : 1));
	assert(b->processMode == p->mode);
	Steinberg_Vst_IParameterChanges *changes = b->inputParameterChanges;
	for (int i = 0; i < changes->lpVtbl->getParameterCount(changes); ++i) {
		Steinberg_Vst_IParamValueQueue *q = changes->lpVtbl->getParameterData(changes, i);
		int index = parameter(q->lpVtbl->getParameterId(q));
		assert(index >= 0 && index < 3);
		for (int j = 0; j < q->lpVtbl->getPointCount(q); ++j) {
			int offset;
			double value;
			assert(q->lpVtbl->getPoint(q, j, &offset, &value) == 0 && offset == 0 && value >= 0 && value <= 1);
			p->values[index] = value;
		}
	}
	for (int i = 0; i < b->inputEvents->lpVtbl->getEventCount(b->inputEvents); ++i) {
		struct Steinberg_Vst_Event event;
		assert(!b->inputEvents->lpVtbl->getEvent(b->inputEvents, i, &event));
		assert(event.sampleOffset == 0 && event.busIndex == 0 && b->numSamples > 0);
		p->note = event.type == Steinberg_Vst_Event_EventTypes_kNoteOnEvent;
		assert(b->outputEvents && !b->outputEvents->lpVtbl->addEvent(b->outputEvents, &event));
	}
	for (int i = 0; i < b->numSamples; ++i) {
		for (int channel = 0; channel < 2; ++channel) {
			float in = p->effect ? b->inputs[0].Steinberg_Vst_AudioBusBuffers_channelBuffers32[channel][i] : p->note;
			float value = in * p->values[0] * p->private_value;
			float out = latency(self) ? p->delay[p->cursor][channel] : value;
			p->delay[p->cursor][channel] = value;
			b->outputs[0].Steinberg_Vst_AudioBusBuffers_channelBuffers32[channel][i] = out;
		}
		p->cursor = (p->cursor + 1) % 7;
	}
	int slot;
	Steinberg_Vst_IParamValueQueue *q =
	    b->outputParameterChanges->lpVtbl->addParameterData(b->outputParameterChanges, &ids[3], &slot);
	assert(q && !q->lpVtbl->addPoint(q, 0, .25, &slot));
	if (b->numSamples > 1) {
		assert(!q->lpVtbl->addPoint(q, b->numSamples - 1, .5, &slot) && slot == 1);
		assert(q->lpVtbl->getPointCount(q) == 2);
	}
	return Steinberg_kResultOk;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE tail(void *s) {
	(void)s;
	return 0;
}
static Steinberg_int32 SMTG_STDMETHODCALLTYPE param_count(void *s) {
	(void)s;
	return 4;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE param_info(
    void *s, Steinberg_int32 index, struct Steinberg_Vst_ParameterInfo *p) {
	(void)s;
	if (index < 0 || index >= 4)
		return Steinberg_kInvalidArgument;
	const char *names[] = {"Gain", "Mode", "Latency", "Meter"};
	*p = (struct Steinberg_Vst_ParameterInfo){.id = ids[index],
	    .stepCount = index == 1 ? 3
	        : index == 2        ? 1
	                            : 0,
	    .defaultNormalizedValue = index == 0 ? .5 : 0,
	    .flags = index == 3 ? Steinberg_Vst_ParameterInfo_ParameterFlags_kIsReadOnly
	                        : Steinberg_Vst_ParameterInfo_ParameterFlags_kCanAutomate};
	if (index == 1)
		p->flags |= Steinberg_Vst_ParameterInfo_ParameterFlags_kIsList |
		    Steinberg_Vst_ParameterInfo_ParameterFlags_kIsProgramChange;
	text(p->title, names[index]);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE param_string(
    void *s, Steinberg_Vst_ParamID id, double value, Steinberg_Vst_String128 out) {
	(void)s;
	char buffer[40];
	snprintf(buffer, sizeof(buffer), "%.3f", value);
	if (id == 7)
		snprintf(buffer, sizeof(buffer), "Mode %d", (int)lround(value * 3));
	text(out, buffer);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE parse_string(
    void *s, Steinberg_Vst_ParamID id, Steinberg_char16 *str, double *out) {
	(void)s;
	(void)id;
	(void)str;
	(void)out;
	return Steinberg_kNotImplemented;
}
static double SMTG_STDMETHODCALLTYPE to_plain(void *s, Steinberg_Vst_ParamID id, double value) {
	(void)s;
	return value * (id == 7 ? 3 : 1);
}
static double SMTG_STDMETHODCALLTYPE to_normal(void *s, Steinberg_Vst_ParamID id, double value) {
	(void)s;
	return value / (id == 7 ? 3 : 1);
}
static double SMTG_STDMETHODCALLTYPE get_param(void *self, Steinberg_Vst_ParamID id) {
	Instance *p = ((Interface *)self)->owner;
	int i = parameter(id);
	return i < 0 ? 0 : p->values[i];
}
static void draw(View *v) {
	if (!v || !v->display)
		return;
	char label[64];
	snprintf(label, sizeof(label), "Gain %.3f / click to set .75", v->owner->values[0]);
	XClearWindow(v->display, v->child);
	XDrawString(v->display, v->child, DefaultGC(v->display, DefaultScreen(v->display)), 10, 25, label, strlen(label));
	XFlush(v->display);
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE set_param(void *self, Steinberg_Vst_ParamID id, double value) {
	Instance *p = ((Interface *)self)->owner;
	int i = parameter(id);
	assert(i >= 0 && value >= 0 && value <= 1);
	int changed = p->values[i] != value;
	p->values[i] = value;
	if (i == 2 && changed && p->handler)
		p->handler->lpVtbl->restartComponent(p->handler, Steinberg_Vst_RestartFlags_kLatencyChanged);
	draw(p->view);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE set_handler(void *self, Steinberg_Vst_IComponentHandler *handler) {
	Instance *p = ((Interface *)self)->owner;
	retain(handler);
	release(p->handler);
	p->handler = handler;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE midi_map(
    void *s, Steinberg_int32 bus, Steinberg_int16 channel, Steinberg_Vst_CtrlNumber cc, Steinberg_Vst_ParamID *id) {
	(void)s;
	assert(bus == 0 && channel >= 0 && channel < 16);
	if (cc != 7)
		return Steinberg_kResultFalse;
	*id = ids[0];
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE connect_peer(void *self, Steinberg_Vst_IConnectionPoint *peer) {
	Instance *p = ((Interface *)self)->owner;
	assert(!p->peer);
	p->peer = peer;
	retain(peer);
	Steinberg_Vst_IHostApplication *h = query(p->host, Steinberg_Vst_IHostApplication_iid);
	Steinberg_Vst_IMessage *m = NULL;
	Steinberg_TUID id;
	memcpy(id, Steinberg_Vst_IMessage_iid, 16);
	assert(!h->lpVtbl->createInstance(h, id, id, (void **)&m));
	m->lpVtbl->setMessageID(m, "fixture");
	Steinberg_Vst_IAttributeList *a = m->lpVtbl->getAttributes(m);
	assert(a && !a->lpVtbl->setInt(a, "number", 42));
	Steinberg_char16 label[] = {'t', 0};
	assert(!a->lpVtbl->setString(a, "label", label));
	assert(!a->lpVtbl->setBinary(a, "data", label, sizeof(label)));
	assert(!peer->lpVtbl->notify(peer, m));
	release(m);
	release(h);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE disconnect_peer(void *self, Steinberg_Vst_IConnectionPoint *peer) {
	Instance *p = ((Interface *)self)->owner;
	assert(p->peer == peer);
	p->peer = NULL;
	release(peer);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE notify(void *s, Steinberg_Vst_IMessage *m) {
	(void)s;
	assert(!strcmp(m->lpVtbl->getMessageID(m), "fixture"));
	Steinberg_Vst_IAttributeList *a = m->lpVtbl->getAttributes(m);
	Steinberg_int64 n;
	assert(!a->lpVtbl->getInt(a, "number", &n) && n == 42);
	Steinberg_char16 label[2];
	assert(!a->lpVtbl->getString(a, "label", label, sizeof(label)) && label[0] == 't' && !label[1]);
	const void *data;
	Steinberg_uint32 size;
	assert(!a->lpVtbl->getBinary(a, "data", &data, &size) && size == sizeof(label) && !memcmp(data, label, size));
	return Steinberg_kResultOk;
}

static Steinberg_uint32 SMTG_STDMETHODCALLTYPE view_ref(void *s) {
	return ++((View *)((Interface *)s)->owner)->refs;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE view_release(void *s) {
	View *v = ((Interface *)s)->owner;
	if (--v->refs)
		return v->refs;
	assert(!v->display && !v->frame);
	v->owner->view = NULL;
	instance_release(&v->owner->controller);
	free(v);
	return 0;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE view_query(void *s, const Steinberg_TUID id, void **out) {
	View *v = ((Interface *)s)->owner;
	*out = NULL;
	if (equal(id, Steinberg_FUnknown_iid) || equal(id, Steinberg_IPlugView_iid))
		*out = &v->view;
	else if (equal(id, Steinberg_Linux_IEventHandler_iid))
		*out = &v->event;
	else if (equal(id, Steinberg_Linux_ITimerHandler_iid))
		*out = &v->timer;
	if (!*out)
		return Steinberg_kNoInterface;
	view_ref(*out);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE supported(void *s, Steinberg_FIDString type) {
	(void)s;
	return !strcmp(type, "X11EmbedWindowID") ? Steinberg_kResultOk : Steinberg_kResultFalse;
}
static void SMTG_STDMETHODCALLTYPE on_fd(void *self, Steinberg_Linux_FileDescriptor fd) {
	View *v = ((Interface *)self)->owner;
	assert(fd == ConnectionNumber(v->display));
	while (XPending(v->display)) {
		XEvent e;
		XNextEvent(v->display, &e);
		if (e.type == ButtonPress) {
			Instance *p = v->owner;
			assert(p->handler);
			p->values[0] = .75;
			assert(!p->handler->lpVtbl->beginEdit(p->handler, ids[0]));
			assert(!p->handler->lpVtbl->performEdit(p->handler, ids[0], .75));
			assert(!p->handler->lpVtbl->endEdit(p->handler, ids[0]));
		}
		draw(v);
	}
}
static void SMTG_STDMETHODCALLTYPE on_timer(void *self) {
	View *v = ((Interface *)self)->owner;
	++v->ticks;
	// Registration changes inside a callback must not invalidate the host's iteration.
	if (v->ticks == 1) {
		assert(!v->loop->lpVtbl->unregisterTimer(v->loop, (void *)&v->timer));
		assert(!v->loop->lpVtbl->registerTimer(v->loop, (void *)&v->timer, 10));
	}
	if (v->display && XPending(v->display))
		on_fd(&v->event, ConnectionNumber(v->display));
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE set_frame(void *self, Steinberg_IPlugFrame *frame) {
	View *v = ((Interface *)self)->owner;
	if (v->loop) {
		assert(!v->loop->lpVtbl->unregisterTimer(v->loop, (void *)&v->timer));
		release(v->loop);
		v->loop = NULL;
	}
	retain(frame);
	release(v->frame);
	v->frame = frame;
	if (frame) {
		v->loop = query(frame, Steinberg_Linux_IRunLoop_iid);
		assert(v->loop);
		void *other = query(factory_host, Steinberg_Linux_IRunLoop_iid);
		assert(other == v->loop);
		release(other);
		assert(!v->loop->lpVtbl->registerTimer(v->loop, (void *)&v->timer, 10));
	}
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE attach(void *self, void *parent, Steinberg_FIDString type) {
	View *v = ((Interface *)self)->owner;
	assert(!supported(self, type) && v->frame && !v->display);
	v->display = XOpenDisplay(NULL);
	if (!v->display)
		return Steinberg_kResultFalse;
	v->child =
	    XCreateSimpleWindow(v->display, (Window)(uintptr_t)parent, 0, 0, v->rect.right, v->rect.bottom, 0, 0, 0xeeeeee);
	XSelectInput(v->display, v->child, ExposureMask | ButtonPressMask);
	XMapWindow(v->display, v->child);
	XFlush(v->display);
	assert(!v->loop->lpVtbl->registerEventHandler(v->loop, (void *)&v->event, ConnectionNumber(v->display)));
	v->rect.right = 260;
	assert(!v->frame->lpVtbl->resizeView(v->frame, (void *)&v->view, &v->rect));
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE removed(void *self) {
	View *v = ((Interface *)self)->owner;
	assert(v->display);
	assert(!v->loop->lpVtbl->unregisterEventHandler(v->loop, (void *)&v->event));
	XDestroyWindow(v->display, v->child);
	XCloseDisplay(v->display);
	v->display = NULL;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE wheel(void *s, float value) {
	(void)s;
	(void)value;
	return Steinberg_kResultFalse;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE key(void *s, Steinberg_char16 c, Steinberg_int16 k, Steinberg_int16 m) {
	(void)s;
	(void)c;
	(void)k;
	(void)m;
	return Steinberg_kResultFalse;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE get_size(void *self, struct Steinberg_ViewRect *rect) {
	*rect = ((View *)((Interface *)self)->owner)->rect;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE on_size(void *self, struct Steinberg_ViewRect *rect) {
	View *v = ((Interface *)self)->owner;
	v->rect = *rect;
	if (v->display) {
		XResizeWindow(v->display, v->child, rect->right - rect->left, rect->bottom - rect->top);
		XFlush(v->display);
	}
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE focus(void *s, Steinberg_TBool state) {
	(void)s;
	(void)state;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE can_resize(void *s) {
	(void)s;
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE constrain(void *s, struct Steinberg_ViewRect *r) {
	(void)s;
	if (r->right - r->left < 100)
		r->right = r->left + 100;
	return Steinberg_kResultOk;
}
static Steinberg_IPlugView *SMTG_STDMETHODCALLTYPE create_view(void *self, Steinberg_FIDString name) {
	Instance *p = ((Interface *)self)->owner;
	if (strcmp(name, "editor") || p->view)
		return NULL;
	static Steinberg_IPlugViewVtbl table = {view_query, view_ref, view_release, supported, attach, removed, wheel, key,
	    key, get_size, on_size, focus, set_frame, can_resize, constrain};
	static Steinberg_Linux_IEventHandlerVtbl event = {view_query, view_ref, view_release, on_fd};
	static Steinberg_Linux_ITimerHandlerVtbl timer = {view_query, view_ref, view_release, on_timer};
	View *v = calloc(1, sizeof(*v));
	assert(v);
	v->view = (Interface){&table, v};
	v->event = (Interface){&event, v};
	v->timer = (Interface){&timer, v};
	v->refs = 1;
	v->owner = p;
	v->rect = (struct Steinberg_ViewRect){0, 0, 240, 100};
	p->view = v;
	instance_ref(&p->controller);
	return (void *)&v->view;
}
static Instance *instance_new(int effect, int control) {
	static Steinberg_Vst_IComponentVtbl component = {instance_query, instance_ref, instance_release, initialize,
	    terminate, controller_class, io_mode, bus_count, bus_info, routing, bus_active, active, load, save};
	static Steinberg_Vst_IAudioProcessorVtbl processor = {instance_query, instance_ref, instance_release, arrangements,
	    arrangement, sample_size, latency, setup, processing, process, tail};
	static Steinberg_Vst_IEditControllerVtbl controller = {instance_query, instance_ref, instance_release, initialize,
	    terminate, load, load, save, param_count, param_info, param_string, parse_string, to_plain, to_normal,
	    get_param, set_param, set_handler, create_view};
	static Steinberg_Vst_IConnectionPointVtbl connection = {
	    instance_query, instance_ref, instance_release, connect_peer, disconnect_peer, notify};
	static Steinberg_Vst_IMidiMappingVtbl mapping = {instance_query, instance_ref, instance_release, midi_map};
	Instance *p = calloc(1, sizeof(*p));
	assert(p);
	++instances;
	p->component = (Interface){&component, p};
	p->processor = (Interface){&processor, p};
	p->controller = (Interface){&controller, p};
	p->connection = (Interface){&connection, p};
	p->mapping = (Interface){&mapping, p};
	p->refs = 1;
	p->effect = effect;
	p->control_only = control;
	p->values[0] = .5;
	p->private_value = 1;
	return p;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE factory_ref(void *s) {
	(void)s;
	return ++factory_refs;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE factory_release(void *s) {
	(void)s;
	assert(factory_refs);
	if (!--factory_refs) {
		release(factory_host);
		factory_host = NULL;
	}
	return factory_refs;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE factory_query(void *self, const Steinberg_TUID id, void **out) {
	*out = NULL;
	if (!equal(id, Steinberg_FUnknown_iid) && !equal(id, Steinberg_IPluginFactory_iid) &&
	    !equal(id, Steinberg_IPluginFactory3_iid))
		return Steinberg_kNoInterface;
	*out = self;
	factory_ref(self);
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE factory_info(void *s, struct Steinberg_PFactoryInfo *out) {
	(void)s;
	memset(out, 0, sizeof(*out));
	strcpy(out->vendor, "Test");
	return Steinberg_kResultOk;
}
static Steinberg_int32 SMTG_STDMETHODCALLTYPE class_count(void *s) {
	(void)s;
	return 3;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE class_info(
    void *s, Steinberg_int32 index, struct Steinberg_PClassInfo *out) {
	(void)s;
	if (index < 0 || index > 2)
		return Steinberg_kInvalidArgument;
	memset(out, 0, sizeof(*out));
	memcpy(out->cid, index == 0 ? synth_id : index == 1 ? effect_id : controller_id, 16);
	out->cardinality = 0x7fffffff;
	strcpy(out->category, index == 2 ? "Component Controller Class" : "Audio Module Class");
	strcpy(out->name, index == 0 ? "VST3 test synth" : index == 1 ? "VST3 test effect" : "Controller");
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE factory_create(
    void *s, Steinberg_FIDString cid, Steinberg_FIDString iid, void **out) {
	(void)s;
	*out = NULL;
	assert(entries);
	if (!equal(cid, synth_id) && !equal(cid, effect_id) && !equal(cid, controller_id))
		return Steinberg_kInvalidArgument;
	Instance *p = instance_new(equal(cid, effect_id), equal(cid, controller_id));
	int result = instance_query(&p->component, iid, out);
	instance_release(&p->component);
	return result;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE class_info2(
    void *s, Steinberg_int32 i, struct Steinberg_PClassInfo2 *out) {
	(void)s;
	(void)i;
	(void)out;
	return Steinberg_kNotImplemented;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE class_unicode(
    void *s, Steinberg_int32 i, struct Steinberg_PClassInfoW *out) {
	(void)s;
	(void)i;
	(void)out;
	return Steinberg_kNotImplemented;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE host_context(void *s, Steinberg_FUnknown *host) {
	(void)s;
	retain(host);
	release(factory_host);
	factory_host = host;
	return Steinberg_kResultOk;
}
char ModuleEntry(void *library) {
	assert(library);
	++entries;
	return 1;
}
char ModuleExit(void) {
	assert(entries && (entries > 1 || (!instances && !factory_host && !factory_refs)));
	--entries;
	return 1;
}
Steinberg_IPluginFactory *GetPluginFactory(void) {
	static Steinberg_IPluginFactory3Vtbl table = {factory_query, factory_ref, factory_release, factory_info,
	    class_count, class_info, factory_create, class_info2, class_unicode, host_context};
	static Steinberg_IPluginFactory3 factory = {&table};
	factory_ref(&factory);
	return (void *)&factory;
}
