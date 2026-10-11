#include <clap/clap.h>
#include <X11/Xlib.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

// Deliberately sparse, out-of-order IDs and a parameter beyond the former 64-slot limit.
enum { PARAMETERS = 130 };
static unsigned entries, instances;
static clap_id parameter_id(uint32_t i) {
	return i == 0 ? 987654321 : 1000 + (PARAMETERS - i) * 7;
}
typedef struct {
	clap_plugin_t plugin;
	const clap_host_t *host;
	const clap_host_thread_check_t *threads;
	const clap_host_params_t *host_params;
	const clap_host_timer_support_t *host_timer;
	_Atomic double values[PARAMETERS];
	atomic_int edited;
	int effect, active, processing, held;
	unsigned limit, latency, position, generation;
	float delay[2][7];
	Display *display;
	Window window;
	clap_id timer;
	double private_state;
} Plugin;
static Plugin *self(const clap_plugin_t *p) {
	return p->plugin_data;
}
static void main_thread(Plugin *p) {
	assert(p->threads->is_main_thread(p->host));
}
static void audio_thread(Plugin *p) {
	assert(p->threads->is_audio_thread(p->host));
}
static bool init(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	p->threads = p->host->get_extension(p->host, CLAP_EXT_THREAD_CHECK);
	p->host_params = p->host->get_extension(p->host, CLAP_EXT_PARAMS);
	p->host_timer = p->host->get_extension(p->host, CLAP_EXT_TIMER_SUPPORT);
	assert(p->threads && p->host_params && p->host_timer);
	main_thread(p);
	p->values[0] = .25;
	p->values[126] = -1.75;
	p->private_state = 1;
	p->host->request_callback(p->host);
	return !getenv("CLAP_TEST_FAIL_INIT");
}
static void destroy(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(!p->active && !p->display);
	assert(entries && instances);
	--instances;
	free(p);
}
static bool activate(const clap_plugin_t *plugin, double rate, uint32_t min, uint32_t max) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(!p->active && rate > 0 && min == 1 && max > 0);
	p->active = 1;
	p->limit = max;
	p->latency = p->effect && p->values[129] >= .5 ? 7 : 0;
	const clap_host_latency_t *latency = p->host->get_extension(p->host, CLAP_EXT_LATENCY);
	assert(latency);
	latency->changed(p->host);
	return true;
}
static void deactivate(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(p->active && !p->processing);
	p->active = 0;
	++p->generation;
	p->host_params->rescan(p->host, CLAP_PARAM_RESCAN_ALL);
}
static bool start(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	audio_thread(p);
	assert(p->active && !p->processing);
	p->processing = 1;
	return true;
}
static void stop(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	audio_thread(p);
	assert(p->processing);
	p->processing = 0;
}
static void reset(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	audio_thread(p);
	assert(p->active);
	p->held = 0;
	memset(p->delay, 0, sizeof(p->delay));
	p->position = 0;
}
static uint32_t param_count(const clap_plugin_t *plugin) {
	main_thread(self(plugin));
	return PARAMETERS;
}
static bool param_info(const clap_plugin_t *plugin, uint32_t index, clap_param_info_t *info) {
	main_thread(self(plugin));
	if (index >= PARAMETERS)
		return false;
	*info = (clap_param_info_t){.id = parameter_id(index),
	    .flags = CLAP_PARAM_IS_AUTOMATABLE | (index == 127 ? CLAP_PARAM_REQUIRES_PROCESS : 0) |
	        (index == 126 ? CLAP_PARAM_IS_STEPPED : 0),
	    .min_value = index == 126 ? -3 : 0,
	    .max_value = index == 126 ? 3 : 1,
	    .default_value = index == 126 ? 1.75
	        : index                   ? 0
	                                  : .25,
	    .cookie = (void *)(uintptr_t)(self(plugin)->generation * PARAMETERS + index + 1)};
	snprintf(info->name, sizeof(info->name), index ? "Control %u" : "Gain", index);
	return true;
}
static bool get_value(const clap_plugin_t *plugin, clap_id id, double *value) {
	Plugin *p = self(plugin);
	main_thread(p);
	for (unsigned i = 0; i < PARAMETERS; ++i)
		if (parameter_id(i) == id) {
			*value = p->values[i];
			return true;
		}
	return false;
}
static bool value_text(const clap_plugin_t *plugin, clap_id id, double value, char *text, uint32_t size) {
	main_thread(self(plugin));
	(void)id;
	snprintf(text, size, "%.1f%%", 100 * value);
	return true;
}
static bool text_value(const clap_plugin_t *plugin, clap_id id, const char *text, double *value) {
	main_thread(self(plugin));
	(void)id;
	*value = strtod(text, NULL) / 100;
	return true;
}
static void events(Plugin *p, const clap_input_events_t *in, const clap_output_events_t *out) {
	for (uint32_t i = 0; i < in->size(in); ++i) {
		const clap_event_header_t *event = in->get(in, i);
		assert(event && event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->time == 0);
		if (event->type == CLAP_EVENT_PARAM_VALUE) {
			const clap_event_param_value_t *v = (const void *)event;
			unsigned n = (uintptr_t)v->cookie - 1 - p->generation * PARAMETERS;
			assert(n < PARAMETERS && v->param_id == parameter_id(n));
			if ((n == 128 || (p->effect && n == 129)) && (p->values[n] >= .5) != (v->value >= .5))
				p->host->request_restart(p->host);
			p->values[n] = v->value;
		} else if (event->type == CLAP_EVENT_NOTE_ON) {
			p->held = 1;
			assert(!out->try_push(out, event)); // The note output is disconnected.
		} else if (event->type == CLAP_EVENT_NOTE_OFF) {
			p->held = 0;
			clap_event_note_t ended = *(const clap_event_note_t *)event;
			ended.header.type = CLAP_EVENT_NOTE_END;
			assert(out->try_push(out, &ended.header));
		} else
			assert(0);
	}
	if (atomic_exchange(&p->edited, 0)) {
		clap_event_param_value_t v = {.header = {.size = sizeof(v), .type = CLAP_EVENT_PARAM_VALUE},
		    .param_id = parameter_id(0),
		    .note_id = -1,
		    .port_index = -1,
		    .channel = -1,
		    .key = -1,
		    .value = p->values[0]};
		assert(out->try_push(out, &v.header));
	}
}
static void flush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
	Plugin *p = self(plugin);
	if (p->active)
		audio_thread(p);
	else
		main_thread(p);
	if (p->active)
		for (uint32_t i = 0; i < in->size(in); ++i) {
			const clap_event_param_value_t *v = (const void *)in->get(in, i);
			assert(v->header.type == CLAP_EVENT_PARAM_VALUE && v->param_id != parameter_id(127));
		}
	events(p, in, out);
}
static clap_process_status process(const clap_plugin_t *plugin, const clap_process_t *block) {
	Plugin *p = self(plugin);
	audio_thread(p);
	assert(p->processing && block->frames_count && block->frames_count <= p->limit);
	assert(block->audio_outputs_count == 1 && block->audio_outputs[0].channel_count == 2);
	assert(block->audio_inputs_count == (uint32_t)p->effect);
	events(p, block->in_events, block->out_events);
	for (uint32_t i = 0; i < block->frames_count; ++i) {
		for (unsigned c = 0; c < 2; ++c) {
			float value = (p->effect ? block->audio_inputs[0].data32[c][i] : p->held) * p->values[0] *
			    p->private_state * (1 + p->values[127]);
			if (p->latency) {
				float delayed = p->delay[c][p->position];
				p->delay[c][p->position] = value;
				value = delayed;
			}
			block->audio_outputs[0].data32[c][i] = value;
		}
		if (++p->position == 7)
			p->position = 0;
	}
	return CLAP_PROCESS_CONTINUE;
}
static void on_main(const clap_plugin_t *plugin) {
	main_thread(self(plugin));
}
static uint32_t audio_count(const clap_plugin_t *plugin, bool input) {
	return input ? self(plugin)->effect : 1;
}
static bool audio_info(const clap_plugin_t *plugin, uint32_t index, bool input, clap_audio_port_info_t *info) {
	main_thread(self(plugin));
	if (index || (input && !self(plugin)->effect))
		return false;
	*info = (clap_audio_port_info_t){.id = input ? 3 : 9,
	    .flags = CLAP_AUDIO_PORT_IS_MAIN,
	    .channel_count = 2,
	    .port_type = CLAP_PORT_STEREO,
	    .in_place_pair = CLAP_INVALID_ID};
	return true;
}
static uint32_t note_count(const clap_plugin_t *plugin, bool input) {
	(void)input;
	// The host accepts synths with a disconnected note output.
	return !self(plugin)->effect;
}
static bool note_info(const clap_plugin_t *plugin, uint32_t index, bool input, clap_note_port_info_t *info) {
	if (index || self(plugin)->effect)
		return false;
	*info = (clap_note_port_info_t){.id = input ? 80 : 81,
	    .supported_dialects = CLAP_NOTE_DIALECT_CLAP,
	    .preferred_dialect = CLAP_NOTE_DIALECT_CLAP};
	return true;
}
static bool save(const clap_plugin_t *plugin, const clap_ostream_t *out) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(!p->processing);
	double data[PARAMETERS + 1];
	for (unsigned i = 0; i < PARAMETERS; ++i)
		data[i] = atomic_load(&p->values[i]);
	data[PARAMETERS] = p->private_state;
	const char *bytes = (const char *)data;
	size_t left = sizeof(data);
	while (left) {
		int64_t n = out->write(out, bytes, left);
		if (n <= 0)
			return false;
		left -= n;
		bytes += n;
	}
	return !getenv("CLAP_TEST_FAIL_SAVE");
}
static bool load(const clap_plugin_t *plugin, const clap_istream_t *in) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(!p->active);
	double data[PARAMETERS + 1];
	char *bytes = (char *)data;
	size_t left = sizeof(data);
	while (left) {
		int64_t n = in->read(in, bytes, left);
		if (n <= 0)
			return false;
		left -= n;
		bytes += n;
	}
	for (unsigned i = 0; i < PARAMETERS; ++i)
		atomic_store(&p->values[i], data[i]);
	p->private_state = data[PARAMETERS];
	p->host_params->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
	return true;
}
static bool gui_supported(const clap_plugin_t *plugin, const char *api, bool floating) {
	main_thread(self(plugin));
	return !floating && api && !strcmp(api, CLAP_WINDOW_API_X11);
}
static bool gui_create(const clap_plugin_t *plugin, const char *api, bool floating) {
	Plugin *p = self(plugin);
	if (!gui_supported(plugin, api, floating) || p->display)
		return false;
	p->display = XOpenDisplay(NULL);
	if (!p->display)
		return false;
	p->window = XCreateSimpleWindow(p->display, DefaultRootWindow(p->display), 0, 0, 240, 90, 0, 0, 0x202020);
	XSelectInput(p->display, p->window, ExposureMask | ButtonPressMask);
	assert(p->host_timer->register_timer(p->host, 10, &p->timer));
	return true;
}
static void gui_destroy(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(p->host_timer->unregister_timer(p->host, p->timer));
	XDestroyWindow(p->display, p->window);
	XCloseDisplay(p->display);
	p->display = NULL;
}
static bool gui_size(const clap_plugin_t *plugin, uint32_t *w, uint32_t *h) {
	main_thread(self(plugin));
	*w = 240;
	*h = 90;
	return true;
}
static bool gui_resizable(const clap_plugin_t *plugin) {
	main_thread(self(plugin));
	return false;
}
static bool gui_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
	Plugin *p = self(plugin);
	main_thread(p);
	XReparentWindow(p->display, p->window, window->x11, 0, 0);
	return true;
}
static bool gui_show(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	XMapWindow(p->display, p->window);
	XFlush(p->display);
	return true;
}
static bool gui_hide(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	XUnmapWindow(p->display, p->window);
	return true;
}
static void timer(const clap_plugin_t *plugin, clap_id id) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(id == p->timer);
	while (XPending(p->display)) {
		XEvent event;
		XNextEvent(p->display, &event);
		if (event.type == ButtonPress) {
			p->values[0] = .75;
			p->edited = 1;
			p->host_params->request_flush(p->host);
		}
	}
	char text[64];
	snprintf(text, sizeof(text), "CLAP gain: %.2f (click to change)", p->values[0]);
	GC gc = DefaultGC(p->display, DefaultScreen(p->display));
	XSetForeground(p->display, gc, 0xffffff);
	XClearWindow(p->display, p->window);
	XDrawString(p->display, p->window, gc, 10, 45, text, strlen(text));
	XFlush(p->display);
}
static uint32_t latency(const clap_plugin_t *plugin) {
	main_thread(self(plugin));
	assert(self(plugin)->active);
	return self(plugin)->latency;
}
static const void *extension(const clap_plugin_t *plugin, const char *id) {
	(void)plugin;
	static const clap_plugin_params_t params = {param_count, param_info, get_value, value_text, text_value, flush};
	static const clap_plugin_audio_ports_t audio = {audio_count, audio_info};
	static const clap_plugin_note_ports_t notes = {note_count, note_info};
	static const clap_plugin_latency_t lag = {latency};
	static const clap_plugin_state_t state = {save, load};
	static const clap_plugin_gui_t gui = {.is_api_supported = gui_supported,
	    .create = gui_create,
	    .destroy = gui_destroy,
	    .get_size = gui_size,
	    .can_resize = gui_resizable,
	    .set_parent = gui_parent,
	    .show = gui_show,
	    .hide = gui_hide};
	static const clap_plugin_timer_support_t timers = {timer};
	if (!strcmp(id, CLAP_EXT_PARAMS))
		return &params;
	if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
		return &audio;
	if (!strcmp(id, CLAP_EXT_NOTE_PORTS))
		return &notes;
	if (!strcmp(id, CLAP_EXT_LATENCY))
		return &lag;
	if (!strcmp(id, CLAP_EXT_STATE))
		return &state;
	if (!strcmp(id, CLAP_EXT_GUI))
		return &gui;
	if (!strcmp(id, CLAP_EXT_TIMER_SUPPORT))
		return &timers;
	return NULL;
}
static const clap_plugin_descriptor_t descriptors[] = {{.clap_version = CLAP_VERSION,
                                                           .id = "org.triccheballacche.test.synth",
                                                           .name = "CLAP test synth",
                                                           .vendor = "triccheballacche",
                                                           .version = "1"},
    {.clap_version = CLAP_VERSION,
        .id = "org.triccheballacche.test.effect",
        .name = "CLAP test effect",
        .vendor = "triccheballacche",
        .version = "1"}};
static uint32_t count(const clap_plugin_factory_t *factory) {
	(void)factory;
	return 2;
}
static const clap_plugin_descriptor_t *descriptor(const clap_plugin_factory_t *factory, uint32_t index) {
	(void)factory;
	return index < 2 ? descriptors + index : NULL;
}
static const clap_plugin_t *create(const clap_plugin_factory_t *factory, const clap_host_t *host, const char *id) {
	(void)factory;
	assert(entries);
	int index = !strcmp(id, descriptors[1].id);
	if (strcmp(id, descriptors[index].id))
		return NULL;
	Plugin *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	++instances;
	p->host = host;
	p->effect = index;
	atomic_init(&p->edited, 0);
	for (unsigned i = 0; i < PARAMETERS; ++i)
		atomic_init(&p->values[i], 0);
	p->plugin = (clap_plugin_t){.desc = descriptors + index,
	    .plugin_data = p,
	    .init = init,
	    .destroy = destroy,
	    .activate = activate,
	    .deactivate = deactivate,
	    .start_processing = start,
	    .stop_processing = stop,
	    .reset = reset,
	    .process = process,
	    .get_extension = extension,
	    .on_main_thread = on_main};
	return &p->plugin;
}
static bool entry_init(const char *path) {
	(void)path;
	++entries;
	return true;
}
static void entry_deinit(void) {
	assert(entries && (entries > 1 || !instances));
	--entries;
}
static const void *factory(const char *id) {
	static const clap_plugin_factory_t f = {count, descriptor, create};
	return !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &f : NULL;
}
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION, .init = entry_init, .deinit = entry_deinit, .get_factory = factory};
