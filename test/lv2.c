#include "score_janet.h"
#include "discovery.h"
#include "host/lv2.h"
#include "host/ui.h"
#include <X11/Xlib.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Window named_window(Display *display, Window parent, const char *name) {
	char *title = NULL;
	if (XFetchName(display, parent, &title) && title) {
		int found = !strcmp(title, name);
		XFree(title);
		if (found)
			return parent;
	}
	Window root, up, *children;
	unsigned count;
	if (!XQueryTree(display, parent, &root, &up, &children, &count))
		return 0;
	Window result = 0;
	for (unsigned i = 0; i < count && !result; ++i)
		result = named_window(display, children[i], name);
	XFree(children);
	return result;
}
static void pump(Session *s, UI *ui) {
	uint64_t position = s->time;
	for (int i = 0; i < 30; ++i) {
		int result = session_poll(s);
		assert(result >= 0 && (!result || !session_restart(s)));
		session_sync(s);
		assert(!ui_poll(ui) && s->time == position);
		nanosleep(&(struct timespec){.tv_nsec = 10000000}, NULL);
	}
}
static void window_test(Session *s) {
	assert(!session_seek(s, llround(.02 * s->sample_rate)));
	UI *ui = NULL;
	assert(ui_available(session_score(s)->nodes) && !ui_open(&ui, s, 0) && ui);
	ui_show(ui, 1);
	pump(s, ui);
	Display *d = XOpenDisplay(NULL);
	assert(d);
	Window parent = named_window(d, DefaultRootWindow(d), "LV2 test synth");
	assert(parent);
	Window root, up, *children;
	unsigned count;
	assert(XQueryTree(d, parent, &root, &up, &children, &count) && count);
	XEvent click = {.xbutton = {.type = ButtonPress,
	                    .display = d,
	                    .window = children[0],
	                    .button = Button1,
	                    .same_screen = True,
	                    .x = 30,
	                    .y = 30}};
	XSendEvent(d, children[0], False, ButtonPressMask, &click);
	XFlush(d);
	XFree(children);
	pump(s, ui);
	double value;
	assert(read_dsp(s->nodes[0].dsp[0].dsp, 0, &value) && value == .75);
	edit_dsp(s->nodes[0].dsp[0].dsp, 0, .625);
	pump(s, ui);
	assert(read_dsp(s->nodes[0].dsp[0].dsp, 0, &value) && value == .625);
	// Release the managed window before testing adapter resizing without WM policy.
	ui_show(ui, 0);
	pump(s, ui);
	XChangeWindowAttributes(d, parent, CWOverrideRedirect, &(XSetWindowAttributes){.override_redirect = True});
	XReparentWindow(d, parent, DefaultRootWindow(d), 0, 0);
	XMapWindow(d, parent);
	XResizeWindow(d, parent, 320, 120);
	XSync(d, False);
	pump(s, ui);
	XWindowAttributes attr;
	assert(XGetWindowAttributes(d, parent, &attr));
	assert(attr.width == 320 && attr.height == 120);
	ui_show(ui, 0);
	ui_show(ui, 1);
	ui_close(ui);
	assert(!ui_open(&ui, s, 0) && ui);
	ui_show(ui, 1);
	pump(s, ui);
	ui_close(ui);
	XCloseDisplay(d);
}
static void latency_test(void) {
	Session s = {0};
	Output output;
	assert(!load_score(&s, &output, "test/lv2/latency.janet"));
	assert(!session_poll(&s) && latency_dsp(s.nodes[1].dsp[0].dsp) == 7);
	s.offline = 1;
	float audio[74];
	while (s.time < s.frames) {
		uint64_t start = s.time;
		size_t n = s.frames - start < 37 ? s.frames - start : 37;
		assert(!session_render(&s, audio, n));
		for (size_t i = 0; i < n; ++i) {
			uint64_t t = start + i;
			float expected =
			    t >= (uint64_t)llround(.01 * s.sample_rate) + 7 && t < (uint64_t)llround(.11 * s.sample_rate) + 7 ? 1
			                                                                                                      : 0;
			assert(audio[i * 2] == expected && audio[i * 2 + 1] == expected);
		}
	}
	session_free(&s);
}
static void messages_test(Session *s) {
	Lv2 *l = s->nodes[0].dsp[0].dsp->instance;
	LV2_Atom_Float atom = {.atom = {sizeof(float), l->map.map(l, LV2_ATOM__Float)}, .body = .625f};
	assert(lv2_ui_send(l, 0, sizeof(atom), &atom) == -1); // Control port.
	assert(lv2_ui_send(l, 3, sizeof(atom), &atom) == -1); // Output port.
	assert(lv2_ui_send(l, UINT32_MAX, sizeof(atom), &atom) == -1);
	assert(lv2_ui_send(l, 2, sizeof(atom), NULL) == -1);
	assert(lv2_ui_send(l, 2, sizeof(LV2_Atom), &atom) == -1); // Truncated body.
	assert(lv2_ui_send(l, 2, sizeof(atom) - 1, &atom) == -1);
	assert(lv2_ui_send(l, 2, MAX_MESSAGE, &atom) == -1);
	for (int i = 0; i < MESSAGE_SLOTS; ++i)
		assert(!lv2_ui_send(l, 2, sizeof(atom), &atom));
	assert(lv2_ui_send(l, 2, sizeof(atom), &atom) == -1);
	uint64_t position = s->time;
	session_sync(s);
	assert(!session_poll(s) && s->time == position);
	assert(!lv2_ui_send(l, 2, sizeof(atom), &atom)); // Draining recovers queue capacity.
	session_sync(s);
}
int main(int argc, char **argv) {
	int ui = argc > 1 && !strcmp(argv[1], "--ui");
	char *json = scan_plugins("build/test/fixture.lv2");
	assert(json && strstr(json, "LV2 test synth") && strstr(json, "LV2 test effect") && !strstr(json, "Unsupported"));
	free(json);
	Modules modules = {0};
	Module *previous = NULL;
	for (int offline = 0; offline < 2; ++offline) {
		Session s = {.modules = &modules, .sample_rate = offline ? 48000 : 44100};
		Output output;
		assert(!load_score(&s, &output, "test/lv2/score.janet"));
		s.offline = offline;
		const PluginConfig *config = &session_score(&s)->nodes[0].config;
		assert(config->format == PLUGIN_LV2 && config->nparams == 6 && config->parameters[4].id == 10);
		DSP *synth = s.nodes[0].dsp[0].dsp;
		Module *alias = module_open(&modules, "./build/test/../test/fixture.lv2", PLUGIN_LV2);
		assert(alias == synth->module && alias == s.nodes[1].dsp[0].dsp->module);
		assert(!previous || alias == previous);
		previous = alias;
		module_close(alias);
		if (offline)
			modules_free(&modules); // Lilv world remains alive until both instances close.
		float audio[74];
		while (s.time < s.frames) {
			uint64_t start = s.time;
			size_t n = s.frames - start < 37 ? s.frames - start : 37;
			assert(!session_poll(&s) && !session_render(&s, audio, n));
			for (size_t i = 0; i < n; ++i) {
				uint64_t t = start + i;
				float expected =
				    t < (uint64_t)llround(.01 * s.sample_rate) || t >= (uint64_t)llround(.11 * s.sample_rate) ? 0
				    : t < (uint64_t)llround(.04 * s.sample_rate)                                              ? .25
				                                                                                              : .375;
				assert(audio[i * 2] == expected && audio[i * 2 + 1] == expected);
			}
		}
		assert(!session_seek(&s, llround(.06 * s.sample_rate)));
		assert(!session_seek(&s, llround(.02 * s.sample_rate)) && !session_render(&s, audio, 3) && audio[0] == .25);
		double value;
		assert(read_dsp(synth, 5, &value) && value == 1); // Worker responses survive reset.
		edit_dsp(synth, 0, .625);
		session_sync(&s);
		assert(!session_render(&s, audio, 3) && audio[0] == .3125);
		messages_test(&s);
		if (ui && !offline)
			window_test(&s);
		session_free(&s);
	}
	modules_free(&modules);
	latency_test();
	puts(
	    "OK: LV2 discovery, feature validation, stereo synth/effect, MIDI/time atoms, automation, scaled controls, workers, seek, latency and module reuse");
	if (ui)
		puts("OK: original LV2 X11 UI, controls and atoms in both directions, resize, close and reopen");
	return 0;
}
