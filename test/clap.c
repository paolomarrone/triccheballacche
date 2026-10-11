#include "score_janet.h"
#include "snapshot.h"
#include "host/dsp_internal.h"
#include "host/ui.h"
#include <X11/Xlib.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void pump(Session *s, UI *ui) {
	for (int i = 0; i < 30; ++i) {
		int request = session_poll(s);
		assert(request >= 0);
		if (request)
			assert(!session_restart(s));
		session_sync(s);
		assert(!ui_poll(ui));
		nanosleep(&(struct timespec){.tv_nsec = 10000000}, NULL);
	}
}
static Window named_window(Display *display, Window parent, const char *name) {
	char *title = NULL;
	if (XFetchName(display, parent, &title) && title) {
		int matches = !strcmp(title, name);
		XFree(title);
		if (matches)
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
static void native_ui(Session *s, int id, int fixture) {
	UI *ui = NULL;
	assert(ui_available(session_score(s)->nodes + id));
	assert(!ui_open(&ui, s, id) && ui);
	ui_show(ui, 1);
	pump(s, ui);
	if (fixture) {
		Display *d = XOpenDisplay(NULL);
		assert(d);
		Window parent = named_window(d, DefaultRootWindow(d), "CLAP test synth");
		assert(parent);
		Window root, up, *children;
		unsigned count;
		assert(XQueryTree(d, parent, &root, &up, &children, &count) && count);
		XEvent event = {.xbutton = {.type = ButtonPress,
		                    .display = d,
		                    .window = children[0],
		                    .button = Button1,
		                    .same_screen = True,
		                    .x = 30,
		                    .y = 30}};
		XSendEvent(d, children[0], False, ButtonPressMask, &event);
		XFlush(d);
		XFree(children);
		XCloseDisplay(d);
		pump(s, ui);
		double value;
		assert(read_dsp(s->nodes[id].dsp[0].dsp, 0, &value) && value == .75);
		edit_dsp(s->nodes[id].dsp[0].dsp, 0, .625);
		pump(s, ui);
		assert(read_dsp(s->nodes[id].dsp[0].dsp, 0, &value) && value == .625);
	}
	ui_show(ui, 0);
	ui_show(ui, 1);
	pump(s, ui);
	ui_close(ui);
	assert(!ui_open(&ui, s, id) && ui);
	ui_show(ui, 1);
	pump(s, ui);
	ui_close(ui);
}
static void latency_test(void) {
	Session s = {0};
	Output output;
	assert(!load_score(&s, &output, "test/clap/latency.janet"));
	assert(!session_poll(&s)); // Initial controls determine latency before the first audio block.
	assert(!session_seek(&s, 0));
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
	edit_dsp(s.nodes[1].dsp[0].dsp, 129, 0);
	session_sync(&s);
	assert(session_poll(&s) == 1 && !session_restart(&s) && !session_poll(&s));
	assert(latency_dsp(s.nodes[1].dsp[0].dsp) == 0 && s.nodes[2].inputs[0].length == 0);
	session_free(&s);
	puts("OK: parallel-path latency compensation, activation notifications and quiescent restart");
}
int main(int argc, char **argv) {
	int ui = argc > 1 && !strcmp(argv[1], "--ui");
	if (argc > 2) {
		Session s = {0};
		Output output;
		assert(!load_score(&s, &output, argv[2]));
		float audio[BLOCK * 2];
		double peak = 0;
		for (int i = 0; i < 100; ++i) {
			int service = session_poll(&s);
			assert(service >= 0);
			if (service)
				assert(!session_restart(&s));
			assert(!session_render(&s, audio, BLOCK));
			for (int j = 0; j < BLOCK * 2; ++j) {
				assert(isfinite(audio[j]));
				peak = fmax(peak, fabs(audio[j]));
			}
		}
		assert(peak > .00001);
		for (int i = 0; i < s.nnodes; ++i)
			if (ui && ui_available(session_score(&s)->nodes + i))
				native_ui(&s, i, 0);
		session_free(&s);
		printf("OK: external plugin audio peak %.6f, native windows opened/closed\n", peak);
		return 0;
	}
	Modules modules = {0};
	Session s = {.modules = &modules};
	Output output;
	assert(!load_score(&s, &output, "test/clap/score.janet"));
	assert(session_score(&s)->nodes[0].config.nparams == 130);
	assert(session_score(&s)->nodes[0].config.parameters[126].value == 1);
	assert(session_score(&s)->nodes[0].config.parameters[129].id == 1007);
	DSP *synth = s.nodes[0].dsp[0].dsp;
	assert(synth && s.nodes[1].dsp[0].dsp);
	Module *alias = module_open(&modules, "./build/test/../test/fixture.clap", PLUGIN_CLAP);
	assert(alias == synth->module && alias == s.nodes[1].dsp[0].dsp->module);
	module_close(alias);
	modules_free(&modules); // Both instances must keep the entry initialized without the cache.
	float audio[BLOCK * 2];
	while (s.time < s.frames) {
		size_t start = s.time, n = s.frames - start < 37 ? s.frames - start : 37;
		assert(session_poll(&s) == 0 && !session_render(&s, audio, n));
		for (size_t j = 0; j < n; ++j) {
			uint64_t t = start + j;
			double expected = t < llround(.01 * s.sample_rate) || t >= llround(.11 * s.sample_rate) ? 0
			    : t < llround(.04 * s.sample_rate)                                                  ? .25
			                                                                                        : .375;
			assert(audio[2 * j] == expected && audio[2 * j + 1] == expected);
		}
	}
	double value;
	assert(read_dsp(synth, 129, &value) && value == .9);
	assert(!session_seek(&s, llround(.06 * s.sample_rate)));
	assert(!session_seek(&s, llround(.02 * s.sample_rate)) && !session_render(&s, audio, 3));
	assert(audio[0] == .25 && audio[1] == .25);
	assert(!session_seek(&s, llround(.02 * s.sample_rate)));
	assert(!session_seek(&s, llround(.06 * s.sample_rate)) && !session_render(&s, audio, 3));
	assert(audio[0] == .375 && audio[1] == .375);
	edit_dsp(synth, 128, 1);
	session_sync(&s);
	assert(!session_seek(&s, llround(.02 * s.sample_rate)));
	edit_dsp(synth, 0, .875);
	session_sync(&s);
	assert(session_poll(&s) == 1 && !session_restart(&s));
	assert(!session_render(&s, audio, 3) && audio[0] == .4375 && audio[1] == .4375);
	for (int i = 0; i <= 1000; ++i) {
		edit_dsp(synth, 127, i / 2000.0);
		session_sync(&s);
	}
	assert(!session_render(&s, audio, 3) && audio[0] == .65625 && audio[1] == .65625);
	edit_dsp(synth, 0, .625);
	session_sync(&s);
	assert(read_dsp(synth, 0, &value) && value == .625);
	assert(!state_dsp(synth, "build/test/clap.state", 1));
	assert(state_dsp(synth, "build/test/missing-directory/clap.state", 1) == -1);
	edit_dsp(synth, 0, .125);
	session_sync(&s);
	assert(!setenv("CLAP_TEST_FAIL_SAVE", "1", 1));
	assert(state_dsp(synth, "build/test/clap.state", 1) == -1);
	assert(!unsetenv("CLAP_TEST_FAIL_SAVE")); // A failed save must leave the previous file intact.
	FILE *state = fopen("build/test/clap.state", "r+b");
	assert(state);
	double private_value = 2;
	assert(!fseek(state, 130 * sizeof(double), SEEK_SET) && fwrite(&private_value, sizeof(double), 1, state) == 1);
	fclose(state);
	assert(!state_dsp(synth, "build/test/clap.state", 0));
	assert(session_poll(&s) == 0);
	assert(read_dsp(synth, 0, &value) && value == .625);
	assert(!session_seek(&s, llround(.06 * s.sample_rate)) && !session_render(&s, audio, 3));
	assert(audio[0] == .75); // Opaque state survives seek; the score restores its own controls.
	if (ui)
		native_ui(&s, 0, 1);
	session_free(&s);
	puts(
	    "OK: CLAP discovery, sparse IDs, 130 controls, synth/effect audio, precise events, stopped edits, state and seek");
	puts("OK: repeated seeks, parameter cookies after restart and coalesced controls requiring process()");
	puts("OK: shared CLAP entry, canonical module paths and instances outliving their cache");
	if (ui)
		puts("OK: CLAP native X11 UI, timers, edits in both directions and repeated window lifetime");
	latency_test();
	return 0;
}
