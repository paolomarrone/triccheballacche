#include "score_janet.h"
#include "discovery.h"
#include "host/vst3.h"
#include "host/ui.h"
#include <X11/Xlib.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uchar.h>

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
	Window parent = named_window(d, DefaultRootWindow(d), "VST3 test synth");
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

static void initialization_failure(void) {
	Module *module = module_open(NULL, "build/test/fixture.vst3", PLUGIN_VST3);
	assert(module);
	Vst v = {0};
	assert(!setenv("VST3_TEST_FAIL_INIT", "1", 1));
	assert(vst_create(&v, module, "ABCDEF01222233334444555566667777") == -1);
	vst_destroy(&v);
	module_close(module);
	char *json = scan_plugins("build/test/fixture.vst3");
	assert(json && strstr(json, "\"error\":\"Initialization or parameter discovery failed\"") &&
	    strstr(json, "ABCDEF01222233334444555566667777"));
	free(json);
	assert(!unsetenv("VST3_TEST_FAIL_INIT"));
}
static void restart_test(Session *s) {
	DSP *dsp = s->nodes[0].dsp[0].dsp;
	Vst *v = dsp->instance;
	const unsigned flags[] = {Steinberg_Vst_RestartFlags_kMidiCCAssignmentChanged,
	    Steinberg_Vst_RestartFlags_kParamTitlesChanged, Steinberg_Vst_RestartFlags_kIoTitlesChanged};
	float audio[6];
	assert(!session_seek(s, llround(.02 * s->sample_rate)));
	assert(!session_render(s, audio, 3) && audio[0] == .25);
	for (size_t i = 0; i < sizeof(flags) / sizeof(*flags); ++i) {
		assert(!v->handler.lpVtbl->restartComponent(&v->handler, flags[i]));
		assert(session_poll(s) == 1 && !session_restart(s));
		// Controller metadata changes must not cut a note already being played.
		assert(!session_render(s, audio, 3) && audio[0] == .25);
	}
}
static void latency_test(void) {
	Session s = {0};
	Output output;
	assert(!load_score(&s, &output, "test/vst3/latency.janet"));
	assert(latency_dsp(s.nodes[1].dsp[0].dsp) == 7);
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
			assert(audio[2 * i] == expected && audio[2 * i + 1] == expected);
		}
	}
	DSP *effect = s.nodes[1].dsp[0].dsp;
	edit_dsp(effect, 2, 0);
	session_sync(&s);
	Vst *v = effect->instance;
	assert(!v->handler.lpVtbl->restartComponent(&v->handler, Steinberg_Vst_RestartFlags_kLatencyChanged));
	assert(session_poll(&s) == 1 && !session_restart(&s));
	assert(!latency_dsp(effect) && !s.nodes[2].inputs[0].length);
	session_free(&s);
}
static void stream_test(void) {
	VstStream *stream = vst_stream_new();
	assert(stream);
	unsigned char data[] = {1, 0, 255}, read[4096];
	Steinberg_int32 count = -1;
	assert(!stream->api.lpVtbl->write(stream, data, sizeof(data), &count) && count == sizeof(data));
	assert(!stream->api.lpVtbl->seek(stream, 0, 0, NULL));
	assert(!stream->api.lpVtbl->read(stream, read, sizeof(read), &count) && count == sizeof(data));
	assert(!memcmp(data, read, sizeof(data)));
	assert(!stream->api.lpVtbl->read(stream, read, sizeof(read), &count) && count == 0);
	vst_release(stream);
}
static void state_test(Session *s) {
	State state = {0};
	DSP *dsp = s->nodes[0].dsp[0].dsp;
	edit_dsp(dsp, 0, .625);
	session_sync(s);
	assert(!state_dsp(dsp, &state, 1));
	edit_dsp(dsp, 0, .125);
	session_sync(s);
	assert(!setenv("VST3_TEST_FAIL_SAVE", "1", 1));
	assert(state_dsp(dsp, &state, 1) == -1);
	assert(!unsetenv("VST3_TEST_FAIL_SAVE"));
	assert(!setenv("VST3_TEST_FAIL_CONTROLLER_SAVE", "1", 1));
	assert(state_dsp(dsp, &state, 1) == -1);
	assert(!unsetenv("VST3_TEST_FAIL_CONTROLLER_SAVE"));
	assert(state_dsp(s->nodes[1].dsp[0].dsp, &state, 0) == -1);
	// An opaque processor value must survive state loading and subsequent seeks.
	double value = 2;
	memcpy(state.data + 32 + 4 * sizeof(double), &value, sizeof(value));
	assert(!state_dsp(dsp, &state, 0));
	assert(!session_poll(s));
	session_sync(s);
	assert(read_dsp(dsp, 0, &value) && value == .625);
	assert(!session_seek(s, llround(.06 * s->sample_rate)));
	float audio[6];
	assert(!session_render(s, audio, 3) && audio[0] == .75);
	assert(!setenv("VST3_TEST_NO_CONTROLLER_STATE", "1", 1));
	assert(!state_dsp(dsp, &state, 1));
	assert(!unsetenv("VST3_TEST_NO_CONTROLLER_STATE"));
	assert(!state_dsp(dsp, &state, 0));
	assert(!session_poll(s));
	state_free(&state);
}
static void reload_test(Session *s) {
	State state = {0};
	DSP *dsp = s->nodes[0].dsp[0].dsp;
	Vst *v = dsp->instance;
	// A full reload needs a new score; a soft restart cannot satisfy it.
	assert(!v->handler.lpVtbl->restartComponent(&v->handler, Steinberg_Vst_RestartFlags_kReloadComponent));
	assert(poll_dsp(dsp) == 1 && restart_dsp(dsp) == -1);
	assert(restart_dsp(dsp) == -1 && poll_dsp(dsp) == -1);
	assert(state_dsp(dsp, &state, 1) == -1 && poll_dsp(dsp) == -1);
}
int main(int argc, char **argv) {
	int ui = argc > 1 && !strcmp(argv[1], "--ui");
	char *json = scan_plugins("build/test/fixture.vst3");
	assert(json && strstr(json, "VST3 test synth") && strstr(json, "VST3 test effect"));
	free(json);
	initialization_failure();
	stream_test();
	Modules modules = {0};
	Module *previous = NULL;
	for (int offline = 0; offline < 2; ++offline) {
		Session s = {.modules = &modules, .sample_rate = offline ? 48000 : 44100};
		Output output;
		assert(!load_score(&s, &output, "test/vst3/score.janet"));
		s.offline = offline;
		const PluginConfig *c = &session_score(&s)->nodes[0].config;
		assert(c->format == PLUGIN_VST3 && c->nparams == 4 && c->parameters[0].id == 987654321);
		assert(c->parameters[1].maximum == 3 && (c->parameters[1].flags & PARAM_INTEGER));
		DSP *synth = s.nodes[0].dsp[0].dsp;
		Module *alias = module_open(&modules, "./build/test/../test/fixture.vst3", PLUGIN_VST3);
		assert(alias == synth->module && alias == s.nodes[1].dsp[0].dsp->module && (!previous || alias == previous));
		previous = alias;
		module_close(alias);
		if (offline)
			modules_free(&modules);
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
				assert(audio[2 * i] == expected && audio[2 * i + 1] == expected);
			}
		}
		double value;
		assert(read_dsp(synth, 1, &value) && value == 2);
		assert(read_dsp(synth, 3, &value) && value == .5);
		assert(!session_seek(&s, llround(.06 * s.sample_rate)));
		assert(!session_seek(&s, llround(.02 * s.sample_rate)) && !session_render(&s, audio, 3) && audio[0] == .25);
		assert(!session_seek(&s, llround(.02 * s.sample_rate)));
		assert(!session_seek(&s, llround(.06 * s.sample_rate)) && !session_render(&s, audio, 3) && audio[0] == .375);
		for (int i = 0; i <= 1000; ++i)
			edit_dsp(synth, 0, i / 2000.0);
		session_sync(&s);
		assert(!session_render(&s, audio, 3) && audio[0] == .25);
		midi_dsp(synth, 0, (const uint8_t[]){0xb0, 7, 127});
		assert(!session_render(&s, audio, 3) && audio[0] == .5);
		assert(read_dsp(synth, 0, &value) && value == 1);
		restart_test(&s);
		state_test(&s);
		if (ui && !offline)
			window_test(&s);
		reload_test(&s);
		session_free(&s);
	}
	modules_free(&modules);
	latency_test();
	if (ui) {
		Session s = {0};
		Output output;
		assert(!load_score(&s, &output, "test/vst3/score.janet"));
		// Independent module handles still share one host context with the factory.
		window_test(&s);
		session_free(&s);
	}
	puts(
	    "OK: VST3 discovery, separate/combined controllers, messages, stable IDs, automation, MIDI mapping, seek, state, latency and module reuse");
	if (ui)
		puts(
		    "OK: VST3 original X11 UI, event loop, reentrant timers, bidirectional controls, resizing, close and reopen");
	return 0;
}
