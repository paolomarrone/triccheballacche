#include <lv2/core/lv2.h>
#include <lv2/atom/util.h>
#include <lv2/atom/forge.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/time/time.h>
#include <lv2/ui/ui.h>
#include <lv2/worker/worker.h>
#include <lv2/state/state.h>
#include <X11/Xlib.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BASE "urn:triccheballacche:test:lv2:"
typedef struct {
	void *ports[13];
	LV2_URID_Map *map;
	LV2_Worker_Schedule *worker;
	LV2_URID sequence, midi, position, object, number, chunk;
	float delay[2][7], rate, private_value;
	int effect, held, active, requested, replied;
	unsigned cursor, capacity;
} Plugin;
static const void *feature(const LV2_Feature *const *features, const char *uri) {
	for (int i = 0; features[i]; ++i)
		if (!strcmp(features[i]->URI, uri))
			return features[i]->data;
	return NULL;
}
static LV2_Handle instantiate(
    const LV2_Descriptor *api, double rate, const char *bundle, const LV2_Feature *const *features) {
	assert(bundle && bundle[strlen(bundle) - 1] == '/');
	Plugin *p = calloc(1, sizeof(*p));
	assert(p);
	p->map = (void *)feature(features, LV2_URID__map);
	p->worker = (void *)feature(features, LV2_WORKER__schedule);
	const LV2_Options_Option *options = feature(features, LV2_OPTIONS__options);
	assert(p->map && p->worker && options);
	LV2_URID max = p->map->map(p->map->handle, LV2_BUF_SIZE__maxBlockLength);
	LV2_URID sequence_size = p->map->map(p->map->handle, LV2_BUF_SIZE__sequenceSize);
	for (const LV2_Options_Option *o = options; o->key; ++o)
		if (o->key == max)
			p->capacity = *(const int32_t *)o->value;
		else if (o->key == sequence_size)
			assert(*(const int32_t *)o->value >= 131072);
	assert(p->capacity);
	p->sequence = p->map->map(p->map->handle, LV2_ATOM__Sequence);
	p->midi = p->map->map(p->map->handle, LV2_MIDI__MidiEvent);
	p->position = p->map->map(p->map->handle, LV2_TIME__Position);
	p->object = p->map->map(p->map->handle, LV2_ATOM__Object);
	p->number = p->map->map(p->map->handle, LV2_ATOM__Float);
	p->chunk = p->map->map(p->map->handle, LV2_ATOM__Chunk);
	p->effect = !strcmp(api->URI, BASE "effect");
	p->rate = rate;
	p->private_value = 1;
	return p;
}
static void connect(LV2_Handle handle, uint32_t port, void *data) {
	Plugin *p = handle;
	assert(port < 13);
	p->ports[port] = data;
}
static void activate(LV2_Handle handle) {
	Plugin *p = handle;
	assert(!p->active);
	p->active = 1;
	p->held = p->cursor = 0;
	memset(p->delay, 0, sizeof(p->delay));
}
static void deactivate(LV2_Handle handle) {
	Plugin *p = handle;
	assert(p->active);
	p->active = 0;
}
static void run(LV2_Handle handle, uint32_t frames) {
	Plugin *p = handle;
	assert(p->active && frames <= p->capacity);
	assert(*(float *)p->ports[10] > 0 && *(float *)p->ports[10] <= p->rate * .5f);
	LV2_Atom_Sequence *in = p->ports[2], *out = p->ports[3];
	assert(in->atom.type == p->sequence);
	uint32_t capacity = out->atom.size;
	assert(capacity >= 131072 - sizeof(LV2_Atom));
	*out = (LV2_Atom_Sequence){.atom = {sizeof(LV2_Atom_Sequence_Body), p->sequence}};
	int position = 0;
	LV2_ATOM_SEQUENCE_FOREACH(in, event) {
		assert(event->time.frames == 0);
		if (event->body.type == p->midi) {
			const uint8_t *m = (const void *)(event + 1);
			p->held = (m[0] & 0xf0) == 0x90 && m[2];
		} else if (event->body.type == p->object) {
			const LV2_Atom_Object *o = (const void *)&event->body;
			position |= o->body.otype == p->position;
		} else if (event->body.type == p->number || event->body.type == p->chunk) {
			assert(lv2_atom_sequence_append_event(out, capacity, event));
		}
	}
	if (frames)
		assert(position);
	int lag = p->effect && *(float *)p->ports[9] >= .5 ? (*(float *)p->ports[12] ? 3 : 7) : 0;
	*(float *)p->ports[8] = lag;
	*(float *)p->ports[1] = *(float *)p->ports[0];
	*(float *)p->ports[11] = p->replied;
	if (frames && !p->requested) {
		uint32_t request = 256;
		assert(!p->worker->schedule_work(p->worker->handle, sizeof(request), &request));
		p->requested = 1;
	}
	for (uint32_t i = 0; i < frames; ++i) {
		for (int c = 0; c < 2; ++c) {
			float value =
			    (p->effect ? ((float *)p->ports[6 + c])[i] : !!p->held) * *(float *)p->ports[0] * p->private_value;
			if (lag) {
				float delayed = p->delay[c][p->cursor];
				p->delay[c][p->cursor] = value;
				value = delayed;
			}
			((float *)p->ports[4 + c])[i] = value;
		}
		p->cursor = (p->cursor + 1) % (lag ? lag : 7);
	}
}
static LV2_Worker_Status work(
    LV2_Handle handle, LV2_Worker_Respond_Function respond, void *context, uint32_t size, const void *data) {
	(void)handle;
	assert(size == sizeof(uint32_t) && *(const uint32_t *)data > 0);
	if (getenv("LV2_TEST_FAIL_WORK"))
		return LV2_WORKER_ERR_UNKNOWN;
	return respond(context, size, data);
}
static LV2_Worker_Status response(LV2_Handle handle, uint32_t size, const void *data) {
	Plugin *p = handle;
	assert(size == sizeof(uint32_t) && *(const uint32_t *)data > 0);
	uint32_t next = *(const uint32_t *)data - 1;
	if (next)
		assert(!p->worker->schedule_work(p->worker->handle, sizeof(next), &next));
	else {
		p->replied = 1;
		*(float *)p->ports[11] = 1;
	}
	return LV2_WORKER_SUCCESS;
}
static LV2_State_Status save(LV2_Handle handle, LV2_State_Store_Function store, LV2_State_Handle context,
    uint32_t flags, const LV2_Feature *const *features) {
	Plugin *p = handle;
	assert(flags & LV2_STATE_IS_PORTABLE);
	assert(!p->requested || p->replied);
	assert(feature(features, LV2_STATE__mapPath) && feature(features, LV2_STATE__freePath));
	if (getenv("LV2_TEST_FAIL_SAVE"))
		return LV2_STATE_ERR_UNKNOWN;
	uint32_t key = p->map->map(p->map->handle, BASE "private"), flags_out = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
	// A host can reject a representation; the plugin may retry with a portable one.
	assert(store(context, key, &p->private_value, sizeof(float), p->number, LV2_STATE_IS_NATIVE) ==
	    LV2_STATE_ERR_BAD_FLAGS);
	LV2_State_Status status = store(context, key, &p->private_value, sizeof(float), p->number, flags_out);
	if (status)
		return status;
	uint32_t type = p->map->map(p->map->handle, LV2_ATOM__URID), uri = p->map->map(p->map->handle, BASE "value");
	return store(context, p->map->map(p->map->handle, BASE "uri"), &uri, sizeof(uri), type, flags_out);
}
static LV2_State_Status restore(LV2_Handle handle, LV2_State_Retrieve_Function retrieve, LV2_State_Handle context,
    uint32_t flags, const LV2_Feature *const *features) {
	(void)flags;
	(void)features;
	Plugin *p = handle;
	assert(!p->requested || p->replied);
	if (getenv("LV2_TEST_FAIL_LOAD"))
		return LV2_STATE_ERR_UNKNOWN;
	size_t size;
	uint32_t type, property_flags;
	const float *value = retrieve(context, p->map->map(p->map->handle, BASE "private"), &size, &type, &property_flags);
	if (!value || size != sizeof(float) || type != p->number)
		return LV2_STATE_ERR_BAD_TYPE;
	p->private_value = *value;
	const uint32_t *uri = retrieve(context, p->map->map(p->map->handle, BASE "uri"), &size, &type, &property_flags);
	if (uri)
		assert(size == sizeof(*uri) && type == p->map->map(p->map->handle, LV2_ATOM__URID) &&
		    *uri == p->map->map(p->map->handle, BASE "value"));
	assert(retrieve(context, p->map->map(p->map->handle, BASE "private"), NULL, NULL, NULL) == value);
	assert(!retrieve(context, p->map->map(p->map->handle, BASE "missing"), NULL, NULL, NULL));
	return LV2_STATE_SUCCESS;
}
static const void *extension(const char *uri) {
	static const LV2_Worker_Interface worker = {work, response, NULL};
	static const LV2_State_Interface state = {save, restore};
	if (!strcmp(uri, LV2_STATE__interface))
		return &state;
	return !strcmp(uri, LV2_WORKER__interface) ? &worker : NULL;
}
static void cleanup(LV2_Handle handle) {
	assert(!((Plugin *)handle)->active);
	free(handle);
}
typedef struct {
	const LV2_Atom_Sequence *input;
	LV2_Atom_Sequence *output;
	LV2_URID sequence, midi, frame_time;
	uint64_t time;
} MidiPlugin;
static LV2_Handle midi_instantiate(
    const LV2_Descriptor *api, double rate, const char *bundle, const LV2_Feature *const *features) {
	(void)api;
	(void)rate;
	(void)bundle;
	MidiPlugin *p = calloc(1, sizeof(*p));
	LV2_URID_Map *map = (void *)feature(features, LV2_URID__map);
	assert(p && map);
	p->sequence = map->map(map->handle, LV2_ATOM__Sequence);
	p->frame_time = map->map(map->handle, LV2_ATOM__frameTime);
	p->midi = map->map(map->handle, LV2_MIDI__MidiEvent);
	return p;
}
static void midi_connect(LV2_Handle handle, uint32_t port, void *data) {
	MidiPlugin *p = handle;
	if (port == 0)
		p->input = data;
	else {
		assert(port == 1);
		p->output = data;
	}
}
static void midi_activate(LV2_Handle handle) {
	((MidiPlugin *)handle)->time = 0;
}
static void midi_run(LV2_Handle handle, uint32_t frames) {
	MidiPlugin *p = handle;
	uint32_t capacity = p->output->atom.size;
	*p->output = (LV2_Atom_Sequence){.atom = {sizeof(LV2_Atom_Sequence_Body), p->sequence}, .body.unit = p->frame_time};
	LV2_ATOM_SEQUENCE_FOREACH(p->input, e) {
		assert(e->body.type == p->midi);
		assert(lv2_atom_sequence_append_event(p->output, capacity, e));
	}
	// Autonomous events at non-block boundaries exercise output timestamps.
	for (int i = 0; i < 2; ++i) {
		uint64_t at = i ? 101 : 37;
		if (p->time <= at && at < p->time + frames) {
			struct {
				LV2_Atom_Event event;
				uint8_t data[8];
			} e = {.event = {.time.frames = at - p->time, .body = {3, p->midi}},
			    .data = {i ? 0x80 : 0x90, 67, i ? 0 : 100}};
			assert(lv2_atom_sequence_append_event(p->output, capacity, &e.event));
		}
	}
	p->time += frames;
}
LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index) {
	static const LV2_Descriptor plugins[] = {
	    {BASE "synth", instantiate, connect, activate, run, deactivate, cleanup, extension},
	    {BASE "effect", instantiate, connect, activate, run, deactivate, cleanup, extension},
	    {BASE "gtk", instantiate, connect, activate, run, deactivate, cleanup, extension},
	    {BASE "midi-output", midi_instantiate, midi_connect, midi_activate, midi_run, NULL, free, NULL}};
	return index < 4 ? plugins + index : NULL;
}

typedef struct {
	Display *display;
	Window window;
	LV2UI_Write_Function write;
	LV2UI_Controller controller;
	LV2UI_Resize resize;
	LV2_URID transfer, number, chunk;
	float gain;
	int sent, received;
} View;
static LV2UI_Handle ui_instantiate(const LV2UI_Descriptor *api, const char *plugin, const char *bundle,
    LV2UI_Write_Function write, void *controller, LV2UI_Widget *widget, const LV2_Feature *const *features) {
	(void)api;
	(void)plugin;
	(void)bundle;
	View *v = calloc(1, sizeof(*v));
	assert(v);
	v->write = write;
	v->controller = controller;
	v->display = XOpenDisplay(NULL);
	assert(v->display);
	Window parent = (Window)(uintptr_t)feature(features, LV2_UI__parent);
	LV2UI_Resize *resize = (void *)feature(features, LV2_UI__resize);
	LV2_URID_Map *map = (void *)feature(features, LV2_URID__map);
	assert(parent && resize && map);
	v->transfer = map->map(map->handle, LV2_ATOM__eventTransfer);
	v->number = map->map(map->handle, LV2_ATOM__Float);
	v->chunk = map->map(map->handle, LV2_ATOM__Chunk);
	v->window = XCreateSimpleWindow(v->display, parent, 0, 0, 240, 90, 0, 0, 0x202020);
	XSelectInput(v->display, v->window, ButtonPressMask | ExposureMask);
	XMapWindow(v->display, v->window);
	XFlush(v->display);
	assert(!resize->ui_resize(resize->handle, 240, 90));
	*widget = (void *)(uintptr_t)v->window;
	return v;
}
static void ui_cleanup(LV2UI_Handle handle) {
	View *v = handle;
	assert(!v->sent || v->received == 3);
	XDestroyWindow(v->display, v->window);
	XCloseDisplay(v->display);
	free(v);
}
static void port_event(LV2UI_Handle handle, uint32_t port, uint32_t size, uint32_t protocol, const void *data) {
	View *v = handle;
	if (!protocol && port == 0) {
		assert(size == sizeof(float));
		v->gain = *(const float *)data;
	} else if (protocol == v->transfer && port == 3) {
		const LV2_Atom_Float *atom = data;
		if (atom->atom.type == v->number) {
			assert(size == sizeof(*atom) && atom->body == .625f);
			v->received |= 1;
		} else {
			assert(atom->atom.type == v->chunk && size == sizeof(LV2_Atom) + 70003 && atom->atom.size == 70003);
			const unsigned char *body = (const unsigned char *)data + sizeof(LV2_Atom);
			for (uint32_t i = 0; i < atom->atom.size; ++i)
				assert(body[i] == (unsigned char)i);
			v->received |= 2;
		}
	}
}
static int idle(LV2UI_Handle handle) {
	View *v = handle;
	while (XPending(v->display)) {
		XEvent event;
		XNextEvent(v->display, &event);
		if (event.type == ButtonPress) {
			float gain = .75f;
			v->write(v->controller, 0, sizeof(gain), 0, &gain);
			LV2_Atom_Float atom = {.atom = {sizeof(float), v->number}, .body = .625f};
			v->write(v->controller, 2, sizeof(atom), v->transfer, &atom);
			LV2_Atom *large = malloc(sizeof(*large) + 70003);
			assert(large);
			*large = (LV2_Atom){70003, v->chunk};
			for (uint32_t i = 0; i < large->size; ++i)
				((unsigned char *)(large + 1))[i] = i;
			v->write(v->controller, 2, sizeof(*large) + large->size, v->transfer, large);
			free(large);
			v->sent = 1;
		}
	}
	char text[64];
	snprintf(text, sizeof(text), "LV2 gain: %.2f (click to change)", v->gain);
	GC gc = DefaultGC(v->display, DefaultScreen(v->display));
	XSetForeground(v->display, gc, 0xffffff);
	XClearWindow(v->display, v->window);
	XDrawString(v->display, v->window, gc, 10, 45, text, strlen(text));
	XFlush(v->display);
	return 0;
}
static int ui_resize(void *handle, int width, int height) {
	View *v = handle;
	XResizeWindow(v->display, v->window, width, height);
	XFlush(v->display);
	return 0;
}
static const void *ui_extension(const char *uri) {
	static const LV2UI_Idle_Interface i = {idle};
	static const LV2UI_Resize r = {NULL, ui_resize};
	if (!strcmp(uri, LV2_UI__idleInterface))
		return &i;
	if (!strcmp(uri, LV2_UI__resize))
		return &r;
	return NULL;
}
LV2_SYMBOL_EXPORT const LV2UI_Descriptor *lv2ui_descriptor(uint32_t index) {
	static const LV2UI_Descriptor ui = {BASE "ui", ui_instantiate, ui_cleanup, port_event, ui_extension};
	return index ? NULL : &ui;
}
