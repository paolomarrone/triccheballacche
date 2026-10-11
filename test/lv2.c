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
	assert(!session_offline(&s, 1));
	assert(!session_poll(&s) && latency_dsp(s.nodes[1].dsp[0].dsp) == 3 && s.nodes[2].inputs[0].length == 6);
	float audio[74];
	while (s.time < s.frames) {
		uint64_t start = s.time;
		size_t n = s.frames - start < 37 ? s.frames - start : 37;
		assert(!session_render(&s, audio, n));
		for (size_t i = 0; i < n; ++i) {
			uint64_t t = start + i;
			float expected =
			    t >= (uint64_t)llround(.01 * s.sample_rate) + 3 && t < (uint64_t)llround(.11 * s.sample_rate) + 3 ? 1
			                                                                                                      : 0;
			assert(audio[i * 2] == expected && audio[i * 2 + 1] == expected);
		}
	}
	assert(!session_offline(&s, 0));
	assert(!session_poll(&s) && latency_dsp(s.nodes[1].dsp[0].dsp) == 7 && s.nodes[2].inputs[0].length == 14);
	session_free(&s);
}
static void messages_test(Session *s) {
	Lv2 *l = s->nodes[0].dsp[0].dsp->instance;
	assert(l->description.ports[2].capacity == 98304 && l->description.ports[3].capacity == 131072);
	LV2_Atom_Float atom = {.atom = {sizeof(float), l->map.map(l, LV2_ATOM__Float)}, .body = .625f};
	assert(lv2_ui_send(l, 0, sizeof(atom), &atom) == -1); // Control port.
	assert(lv2_ui_send(l, 3, sizeof(atom), &atom) == -1); // Output port.
	assert(lv2_ui_send(l, UINT32_MAX, sizeof(atom), &atom) == -1);
	assert(lv2_ui_send(l, 2, sizeof(atom), NULL) == -1);
	assert(lv2_ui_send(l, 2, sizeof(LV2_Atom), &atom) == -1); // Truncated body.
	assert(lv2_ui_send(l, 2, sizeof(atom) - 1, &atom) == -1);
	assert(lv2_ui_send(l, 2, sizeof(atom) + 1, &atom) == -1); // Extra bytes after the body.
	for (int i = 0; i < MESSAGE_SLOTS; ++i)
		assert(!lv2_ui_send(l, 2, sizeof(atom), &atom));
	assert(lv2_ui_send(l, 2, sizeof(atom), &atom) == -1);
	uint64_t position = s->time;
	session_sync(s);
	assert(!session_poll(s) && s->time == position);
	assert(!lv2_ui_send(l, 2, sizeof(atom), &atom)); // Draining recovers queue capacity.
	session_sync(s);

	// A non-aligned payload crosses both the former 4 KiB message and 64 KiB port limits.
	size_t bytes = sizeof(LV2_Atom) + 70003;
	LV2_Atom *large = malloc(bytes);
	uint64_t *reply = malloc(l->to_ui.limit);
	assert(large && reply);
	*large = (LV2_Atom){70003, l->chunk};
	for (uint32_t i = 0; i < large->size; ++i)
		((unsigned char *)(large + 1))[i] = i;
	watch_dsp(l->dsp, 1);
	size_t size;
	for (int playing = 0; playing < 2; ++playing) {
		// Each message fits, but the burst must span several processing calls.
		for (int i = 0; i < 3; ++i) {
			((unsigned char *)(large + 1))[0] = i;
			assert(!lv2_ui_send(l, 2, bytes, large));
		}
		for (int i = 0; i < 3; ++i) {
			if (playing) {
				float audio[2];
				assert(!session_render(s, audio, 1));
			} else
				session_sync(s);
			assert(!session_poll(s));
			assert(message_pop(&l->to_ui, &size, reply) && size == bytes + sizeof(uint64_t) && reply[0] == 3);
			((unsigned char *)(large + 1))[0] = i;
			assert(!memcmp(reply + 1, large, bytes) && !atomic_load(&l->dsp->overflow));
			assert(!message_pop(&l->to_ui, &size, reply));
		}
	}
	large->size = l->description.ports[2].capacity;
	assert(lv2_ui_send(l, 2, sizeof(*large) + large->size, large) == -1);

	// An atom that fills the port alone must be rejected: transport needs room too.
	uint32_t capacity = l->description.ports[2].capacity;
	free(large);
	large = calloc(1, capacity);
	assert(large);
	*large = (LV2_Atom){capacity - sizeof(LV2_Atom_Sequence) - sizeof(LV2_Atom_Event), l->chunk};
	assert(lv2_ui_send(l, 2, sizeof(*large) + large->size, large) == -1);
	// Find the largest accepted aligned payload, without duplicating transport serialization.
	do {
		large->size -= 8;
		assert(large->size >= capacity - 512);
		bytes = sizeof(*large) + large->size;
	} while (lv2_ui_send(l, 2, bytes, large));
	session_sync(s);
	assert(!session_poll(s) && !atomic_load(&l->dsp->overflow));
	assert(message_pop(&l->to_ui, &size, reply) && size == bytes + sizeof(uint64_t) && reply[0] == 3);
	assert(!memcmp(reply + 1, large, bytes) && !message_pop(&l->to_ui, &size, reply));
	watch_dsp(l->dsp, 0);
	free(reply);
	free(large);
}
static void discovery_test(void) {
	char *json = scan_plugins("build/test/fixture.lv2");
	assert(json && strstr(json, "LV2 test synth") && strstr(json, "LV2 test effect"));
	assert(strstr(json, "Unsupported required feature: urn:unsupported:feature"));
	assert(strstr(json, "Unsupported required option: urn:unsupported:option"));
	assert(strstr(json, "\"midi-output-bus\":1"));
	assert(strstr(json, "Plugins without audio or MIDI outputs are not supported"));
	assert(strstr(json, "Port 0 requires 2097152 bytes (limit 1048576)"));
	assert(strstr(json, "Port 0 has an invalid minimum buffer size"));
	assert(strstr(json, "\"ui-error\":"));
	assert(strstr(json, "\"id\":\"gain\",\"initialValue\":0.75"));
	free(json);
	Score score = {0};
	Output output;
	char *error = NULL;
	assert(prepare_score(&score, &output, "test/lv2/score.janet",
	    "(daw/plugin {:format :lv2 :path \"build/test/fixture.lv2\" :id \"urn:triccheballacche:test:lv2:midi-output\"})",
	    &error, NULL));
	assert(error && strstr(error, "Missing (daw/end"));
	free(error);
	score_free(&score);
}
static void defaults_test(void) {
	const char *source =
	    "(def s (daw/plugin {:format :lv2 :path \"build/test/fixture.lv2\" :id \"urn:triccheballacche:test:lv2:synth\"}))"
	    "(daw/output s) (daw/note s 0 0.1 60 100) (daw/end 0.2)";
	for (int offline = 0; offline < 2; ++offline) {
		Score score = {.sample_rate = offline ? 48000 : 44100};
		Session s = {0};
		Output output;
		assert(!prepare_score(&score, &output, "test/lv2/score.janet", source, NULL, NULL));
		assert(!session_activate(&s, &score) && !session_offline(&s, offline));
		DSP *dsp = s.nodes[0].dsp[0].dsp;
		Lv2 *l = dsp->instance;
		double value;
		assert(read_dsp(dsp, 0, &value) && value == .75);
		assert(read_dsp(dsp, 4, &value) && value == 10000); // State values are not rate-scaled bounds.
		lv2_worker_begin(l, offline);
		uint32_t request = 256;
		assert(!lv2_schedule(l, sizeof(request), &request));
		lv2_worker_wait(l); // Responses schedule more work, beyond a single queue-sized drain.
		assert(l->description.ports[11].value == 1 && !lv2_worker_pending(l));
		edit_dsp(dsp, 0, .125);
		session_sync(&s);
		float audio[2];
		assert(!session_seek(&s, 0) && !session_render(&s, audio, 1) && audio[0] == .75);
		session_free(&s);
	}
	puts("OK: LV2 initial state, explicit score values, sample-rate bounds and recursive worker drain");
}
static const void *seed(LV2_State_Handle handle, uint32_t key, size_t *size, uint32_t *type, uint32_t *flags) {
	Lv2 *l = handle;
	static const float value = 2;
	if (key != lv2_map_uri(l, "urn:triccheballacche:test:lv2:private"))
		return NULL;
	if (size)
		*size = sizeof(value);
	if (type)
		*type = lv2_map_uri(l, LV2_ATOM__Float);
	if (flags)
		*flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
	return &value;
}
static void state_test(void) {
	Session s = {0};
	Output output;
	assert(!load_score(&s, &output, "test/lv2/score.janet"));
	DSP *dsp = s.nodes[0].dsp[0].dsp;
	Lv2 *l = dsp->instance;
	const LV2_State_Interface *api = lilv_instance_get_extension_data(l->instance, LV2_STATE__interface);
	State state = {0};
	float first[2];
	assert(!session_render(&s, first, 1)); // Queues a chain of asynchronous worker jobs.
	edit_dsp(dsp, 0, .625);
	assert(!session_state(&s, 0, &state, 1));
	assert(l->description.ports[11].value == 1 && !lv2_worker_pending(l));

	assert(!api->restore(lilv_instance_get_handle(l->instance), seed, l, 0, l->feature_ptrs));
	edit_dsp(dsp, 0, .125);
	assert(!setenv("LV2_TEST_FAIL_SAVE", "1", 1));
	assert(session_state(&s, 0, &state, 1) == -1);
	assert(!unsetenv("LV2_TEST_FAIL_SAVE"));
	assert(!setenv("LV2_TEST_FAIL_LOAD", "1", 1));
	assert(session_state(&s, 0, &state, 0) == -1);
	assert(!unsetenv("LV2_TEST_FAIL_LOAD"));
	assert(!session_state(&s, 0, &state, 0));
	double value;
	assert(read_dsp(dsp, 0, &value) && value == .625);
	assert(session_state(&s, 1, &state, 0) == -1); // Different plugin URI.
	assert(!session_seek(&s, llround(.06 * s.sample_rate)));
	float audio[6];
	assert(!session_render(&s, audio, 3) && audio[0] == .375); // Opaque gain restored to one.
	--state.length;
	assert(session_state(&s, 0, &state, 0) == -1);
	++state.length;
	// Restore into a fresh instance with a different sample rate and independently assigned URIDs.
	session_free(&s);
	s.sample_rate = 48000;
	assert(!load_score(&s, &output, "test/lv2/score.janet"));
	assert(!session_state(&s, 0, &state, 0));
	assert(!session_render(&s, audio, 3)); // Fixture checks the rate-scaled control.
	dsp = s.nodes[0].dsp[0].dsp;
	l = dsp->instance;
	lv2_worker_begin(l, 1);
	assert(!setenv("LV2_TEST_FAIL_WORK", "1", 1));
	uint32_t request = 1;
	assert(!lv2_schedule(l, sizeof(request), &request));
	assert(!unsetenv("LV2_TEST_FAIL_WORK"));
	assert(state_dsp(dsp, &state, 1) == -1); // A worker failure must not produce a successful snapshot.
	state_free(&state);
	session_free(&s);
	puts("OK: LV2 ports, opaque properties, portable URIDs, sample rates, failed saves/loads and malformed presets");
}

int main(int argc, char **argv) {
	defaults_test();
	state_test();
	int ui = argc > 1 && !strcmp(argv[1], "--ui");
	discovery_test();
	Modules modules = {0};
	Module *previous = NULL;
	for (int offline = 0; offline < 2; ++offline) {
		Session s = {.modules = &modules, .sample_rate = offline ? 48000 : 44100};
		Output output;
		assert(!load_score(&s, &output, "test/lv2/score.janet"));
		assert(!session_offline(&s, offline));
		const PluginConfig *config = &session_score(&s)->nodes[0].config;
		assert(config->format == PLUGIN_LV2 && config->nparams == 7 && config->parameters[4].id == 10);
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
