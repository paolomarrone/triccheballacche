#include <lv2/core/lv2.h>
#include <lv2/atom/util.h>
#include <lv2/atom/forge.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/time/time.h>
#include <lv2/ui/ui.h>
#include <lv2/worker/worker.h>
#include <X11/Xlib.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BASE "urn:triccheballacche:test:lv2:"
typedef struct {
	void *ports[12];
	LV2_URID_Map *map;
	LV2_Worker_Schedule *worker;
	LV2_URID sequence, midi, position, object, number;
	float delay[2][7], rate;
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
	for (const LV2_Options_Option *o = options; o->key; ++o)
		if (o->key == max)
			p->capacity = *(const int32_t *)o->value;
	assert(p->capacity);
	p->sequence = p->map->map(p->map->handle, LV2_ATOM__Sequence);
	p->midi = p->map->map(p->map->handle, LV2_MIDI__MidiEvent);
	p->position = p->map->map(p->map->handle, LV2_TIME__Position);
	p->object = p->map->map(p->map->handle, LV2_ATOM__Object);
	p->number = p->map->map(p->map->handle, LV2_ATOM__Float);
	p->effect = !strcmp(api->URI, BASE "effect");
	p->rate = rate;
	return p;
}
static void connect(LV2_Handle handle, uint32_t port, void *data) {
	Plugin *p = handle;
	assert(port < 12);
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
	assert(*(float *)p->ports[10] == p->rate * .25f);
	LV2_Atom_Sequence *in = p->ports[2], *out = p->ports[3];
	assert(in->atom.type == p->sequence);
	uint32_t capacity = out->atom.size;
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
		} else if (event->body.type == p->number) {
			assert(lv2_atom_sequence_append_event(out, capacity, event));
		}
	}
	if (frames)
		assert(position);
	int lag = p->effect && *(float *)p->ports[9] >= .5 ? 7 : 0;
	*(float *)p->ports[8] = lag;
	*(float *)p->ports[1] = *(float *)p->ports[0];
	*(float *)p->ports[11] = p->replied;
	if (frames && !p->requested) {
		uint32_t request = 42;
		assert(!p->worker->schedule_work(p->worker->handle, sizeof(request), &request));
		p->requested = 1;
	}
	for (uint32_t i = 0; i < frames; ++i) {
		for (int c = 0; c < 2; ++c) {
			float value = (p->effect ? ((float *)p->ports[6 + c])[i] : !!p->held) * *(float *)p->ports[0];
			if (lag) {
				float delayed = p->delay[c][p->cursor];
				p->delay[c][p->cursor] = value;
				value = delayed;
			}
			((float *)p->ports[4 + c])[i] = value;
		}
		p->cursor = (p->cursor + 1) % 7;
	}
}
static LV2_Worker_Status work(
    LV2_Handle handle, LV2_Worker_Respond_Function respond, void *context, uint32_t size, const void *data) {
	(void)handle;
	assert(size == sizeof(uint32_t) && *(const uint32_t *)data == 42);
	return respond(context, size, data);
}
static LV2_Worker_Status response(LV2_Handle handle, uint32_t size, const void *data) {
	Plugin *p = handle;
	assert(size == sizeof(uint32_t) && *(const uint32_t *)data == 42);
	p->replied = 1;
	return LV2_WORKER_SUCCESS;
}
static const void *extension(const char *uri) {
	static const LV2_Worker_Interface worker = {work, response, NULL};
	return !strcmp(uri, LV2_WORKER__interface) ? &worker : NULL;
}
static void cleanup(LV2_Handle handle) {
	assert(!((Plugin *)handle)->active);
	free(handle);
}
LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index) {
	static const LV2_Descriptor plugins[] = {
	    {BASE "synth", instantiate, connect, activate, run, deactivate, cleanup, extension},
	    {BASE "effect", instantiate, connect, activate, run, deactivate, cleanup, extension}};
	return index < 2 ? plugins + index : NULL;
}

typedef struct {
	Display *display;
	Window window;
	LV2UI_Write_Function write;
	LV2UI_Controller controller;
	LV2UI_Resize resize;
	LV2_URID transfer, number;
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
	assert(!v->sent || v->received);
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
		assert(size == sizeof(*atom) && atom->atom.type == v->number && atom->body == .625f);
		v->received = 1;
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
