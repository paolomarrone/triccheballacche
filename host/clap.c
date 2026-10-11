#include "clap.h"
#include "platform.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	void *library;
	const clap_plugin_entry_t *entry;
} ClapModule;

static void close_module(void *data) {
	ClapModule *m = data;
	if (!m)
		return;
	if (m->entry)
		m->entry->deinit();
	library_close(m->library);
	free(m);
}
static void *open_module(const char *path) {
	ClapModule *m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;
	m->library = library_open(path);
	const clap_plugin_entry_t *entry = m->library ? library_symbol(m->library, "clap_entry") : NULL;
	if (!entry || !clap_version_is_compatible(entry->clap_version) || !entry->init || !entry->deinit ||
	    !entry->get_factory || !entry->init(path)) {
		close_module(m);
		return NULL;
	}
	m->entry = entry;
	return m;
}
const ModuleOps clap_module_ops = {open_module, close_module};
const clap_plugin_entry_t *clap_module_entry(Module *module) {
	return ((ClapModule *)module_data(module))->entry;
}

static _Thread_local Clap *audio_owner;
// The address identifies the creating thread without exposing an OS thread handle.
static _Thread_local char thread_id;
static Clap *host(const clap_host_t *h) {
	return h->host_data;
}
static void request_callback(const clap_host_t *h) {
	atomic_fetch_or(&host(h)->requests, CLAP_HOST_CALLBACK);
}
static void request_restart(const clap_host_t *h) {
	atomic_fetch_or(&host(h)->requests, CLAP_HOST_RESTART);
}
static void request_flush(const clap_host_t *h) {
	atomic_fetch_or(&host(h)->requests, CLAP_HOST_FLUSH);
}
static bool main_thread(const clap_host_t *h) {
	return host(h)->main_thread == &thread_id;
}
static bool audio_thread(const clap_host_t *h) {
	return audio_owner == host(h);
}
static void rescan(const clap_host_t *h, clap_param_rescan_flags flags) {
	atomic_fetch_or(&host(h)->requests, (flags & CLAP_PARAM_RESCAN_ALL ? CLAP_HOST_RESTART : 0) | CLAP_HOST_VALUES);
}
static void clear(const clap_host_t *h, clap_id id, clap_param_clear_flags flags) {
	(void)id;
	(void)flags;
	request_restart(h); // A prepared score cannot change its parameter identity in place.
}
static void dirty(const clap_host_t *h) {
	(void)h;
}
static bool port_scan_supported(const clap_host_t *h, uint32_t flag) {
	(void)h;
	return !(flag &
	    ~(CLAP_AUDIO_PORTS_RESCAN_NAMES | CLAP_AUDIO_PORTS_RESCAN_FLAGS | CLAP_AUDIO_PORTS_RESCAN_CHANNEL_COUNT |
	        CLAP_AUDIO_PORTS_RESCAN_PORT_TYPE | CLAP_AUDIO_PORTS_RESCAN_IN_PLACE_PAIR | CLAP_AUDIO_PORTS_RESCAN_LIST));
}
static void port_rescan(const clap_host_t *h, uint32_t flags) {
	(void)flags;
	request_restart(h);
}
static uint32_t note_dialects(const clap_host_t *h) {
	(void)h;
	return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
}
static void note_rescan(const clap_host_t *h, uint32_t flags) {
	(void)flags;
	request_restart(h);
}
static void log_message(const clap_host_t *h, clap_log_severity severity, const char *text) {
	if (audio_thread(h)) {
		if (severity >= CLAP_LOG_ERROR)
			atomic_fetch_or(&host(h)->requests, CLAP_HOST_ERROR);
	} else
		fprintf(stderr, "[CLAP] %s\n", text);
}
static bool register_timer(const clap_host_t *h, uint32_t period, clap_id *id) {
	Clap *c = host(h);
	if (!period || c->ntimers == 64 || c->next_timer == CLAP_INVALID_ID)
		return false;
	*id = c->next_timer++;
	c->timers[c->ntimers++] = (ClapTimer){*id, period, host_clock() + period};
	return true;
}
static bool unregister_timer(const clap_host_t *h, clap_id id) {
	Clap *c = host(h);
	for (unsigned i = 0; i < c->ntimers; ++i)
		if (c->timers[i].id == id) {
			c->timers[i] = c->timers[--c->ntimers];
			return true;
		}
	return false;
}
static void resize_hints(const clap_host_t *h) {
	(void)h;
}
static bool resize_gui(const clap_host_t *h, uint32_t w, uint32_t height) {
	Clap *c = host(h);
	return main_thread(h) && c->resize && c->resize(c->window, w, height);
}
static bool show_gui(const clap_host_t *h) {
	Clap *c = host(h);
	if (!main_thread(h) || !c->show)
		return false;
	c->show(c->window, 1);
	return true;
}
static bool hide_gui(const clap_host_t *h) {
	Clap *c = host(h);
	if (!main_thread(h) || !c->show)
		return false;
	c->show(c->window, 0);
	return true;
}
static void closed_gui(const clap_host_t *h, bool destroyed) {
	(void)destroyed;
	atomic_fetch_or(&host(h)->requests, CLAP_HOST_CLOSED);
}
static const void *extension(const clap_host_t *h, const char *id) {
	(void)h;
	static const clap_host_thread_check_t threads = {main_thread, audio_thread};
	static const clap_host_params_t params = {rescan, clear, request_flush};
	static const clap_host_state_t state = {dirty};
	static const clap_host_latency_t latency = {dirty};
	static const clap_host_audio_ports_t ports = {port_scan_supported, port_rescan};
	static const clap_host_note_ports_t notes = {note_dialects, note_rescan};
	static const clap_host_log_t log = {log_message};
	static const clap_host_timer_support_t timer = {register_timer, unregister_timer};
	static const clap_host_gui_t gui = {resize_hints, resize_gui, show_gui, hide_gui, closed_gui};
	const struct {
		const char *id;
		const void *api;
	} extensions[] = {{CLAP_EXT_THREAD_CHECK, &threads}, {CLAP_EXT_PARAMS, &params}, {CLAP_EXT_STATE, &state},
	    {CLAP_EXT_LATENCY, &latency}, {CLAP_EXT_AUDIO_PORTS, &ports}, {CLAP_EXT_NOTE_PORTS, &notes},
	    {CLAP_EXT_LOG, &log}, {CLAP_EXT_TIMER_SUPPORT, &timer}, {CLAP_EXT_GUI, &gui}};
	for (size_t i = 0; i < sizeof(extensions) / sizeof(*extensions); ++i)
		if (!strcmp(id, extensions[i].id))
			return extensions[i].api;
	return clap_platform_extension(id);
}

int clap_create(Clap *c, Module *module, const char *id) {
	c->main_thread = &thread_id;
	atomic_init(&c->requests, 0);
	c->host = (clap_host_t){.clap_version = CLAP_VERSION,
	    .host_data = c,
	    .name = "triccheballacche",
	    .vendor = "triccheballacche",
	    .url = "https://github.com/paolomarrone/triccheballacche",
	    .version = "dev",
	    .get_extension = extension,
	    .request_restart = request_restart,
	    .request_process = request_flush,
	    .request_callback = request_callback};
	if (clap_platform_open(c))
		return -1;
	const clap_plugin_entry_t *entry = clap_module_entry(module);
	const clap_plugin_factory_t *factory = entry ? entry->get_factory(CLAP_PLUGIN_FACTORY_ID) : NULL;
	if (!factory || !id || !(c->plugin = factory->create_plugin(factory, &c->host, id)))
		return -1;
	const clap_plugin_t *p = c->plugin;
	if (!p->init || !p->destroy || !p->activate || !p->deactivate || !p->start_processing || !p->stop_processing ||
	    !p->reset || !p->process || !p->get_extension || !p->on_main_thread || !p->init(p))
		return -1;
	c->params = p->get_extension(p, CLAP_EXT_PARAMS);
	c->audio = p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
	c->notes = p->get_extension(p, CLAP_EXT_NOTE_PORTS);
	c->state = p->get_extension(p, CLAP_EXT_STATE);
	c->latency = p->get_extension(p, CLAP_EXT_LATENCY);
	c->render = p->get_extension(p, CLAP_EXT_RENDER);
	c->gui = p->get_extension(p, CLAP_EXT_GUI);
	c->timer = p->get_extension(p, CLAP_EXT_TIMER_SUPPORT);
	return clap_parameters(c);
}

static void suspend(Clap *c) {
	if (c->processing) {
		audio_owner = c;
		c->plugin->stop_processing(c->plugin);
		audio_owner = NULL;
		c->processing = 0;
	}
	if (c->active) {
		c->plugin->deactivate(c->plugin);
		c->active = 0;
	}
}
void clap_destroy(Clap *c) {
	suspend(c);
	if (c->plugin && c->plugin->destroy)
		c->plugin->destroy(c->plugin);
	clap_platform_close(c);
	free(c->info);
	free(c->by_id);
	free(c->events);
	free(c->pending);
	free(c->inputs);
	free(c->outputs);
	free(c->input_channels);
	free(c->output_channels);
	free(c->silence);
	free(c->scratch);
}

static int parameter_index(Clap *c, clap_id id) {
	uint32_t lo = 0, hi = c->nparams;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (c->by_id[mid].id < id)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < c->nparams && c->by_id[lo].id == id ? c->by_id[lo].index : -1;
}
static uint32_t event_count(const clap_input_events_t *list) {
	return ((Clap *)list->ctx)->count;
}
static const clap_event_header_t *event_get(const clap_input_events_t *list, uint32_t index) {
	Clap *c = list->ctx;
	return index < c->count ? &c->events[index].header : NULL;
}
// Both CLAP and raw MIDI events may address the same output voices.
static bool midi_output(Clap *c, uint32_t frame, const uint8_t *data, size_t size, int32_t id) {
	int type = data[0] & 0xf0, channel = data[0] & 15;
	int on = size == 3 && type == 0x90 && data[2];
	if (on && c->nheld == MIDI_EVENTS) {
		atomic_fetch_or(&c->requests, CLAP_HOST_ERROR);
		return false;
	}
	if (dsp_midi_output(c->dsp, frame, data, size))
		return false;
	if (on)
		c->held[c->nheld++] = (ClapNote){id, channel, data[1]};
	else if (data[0] == 0xff)
		c->nheld = 0;
	else if (type == 0x80 || type == 0x90 || (type == 0xb0 && (data[1] == 120 || data[1] == 123)))
		for (unsigned i = 0; i < c->nheld;) {
			ClapNote *note = c->held + i;
			if (note->channel == channel && (type == 0xb0 || note->key == data[1]))
				memmove(note, note + 1, (--c->nheld - i) * sizeof(*note));
			else
				++i;
		}
	return true;
}
static bool event_push(const clap_output_events_t *list, const clap_event_header_t *event) {
	Clap *c = list->ctx;
	if (event->space_id != CLAP_CORE_EVENT_SPACE_ID)
		return false;
	if (event->type == CLAP_EVENT_PARAM_VALUE && event->size >= sizeof(clap_event_param_value_t)) {
		const clap_event_param_value_t *p = (const void *)event;
		int i = parameter_index(c, p->param_id);
		double value = p->value;
		if (i < 0 || p->note_id != -1 || p->port_index != -1 || p->channel != -1 || p->key != -1 ||
		    !clap_value(c->info + i, &value))
			return false;
		if (c->dsp)
			atomic_store(&c->dsp->values[i], value);
		return true;
	}
	if (event->type == CLAP_EVENT_MIDI && event->size >= sizeof(clap_event_midi_t)) {
		const clap_event_midi_t *e = (const void *)event;
		return e->port_index == 0 && midi_output(c, event->time, e->data, midi_size(e->data[0]), -1);
	}
	if (event->type == CLAP_EVENT_MIDI_SYSEX && event->size >= sizeof(clap_event_midi_sysex_t)) {
		const clap_event_midi_sysex_t *e = (const void *)event;
		return e->port_index == 0 && !dsp_midi_output(c->dsp, event->time, e->buffer, e->size);
	}
	if ((event->type == CLAP_EVENT_NOTE_ON || event->type == CLAP_EVENT_NOTE_OFF ||
	        event->type == CLAP_EVENT_NOTE_CHOKE) &&
	    event->size >= sizeof(clap_event_note_t)) {
		const clap_event_note_t *e = (const void *)event;
		int on = event->type == CLAP_EVENT_NOTE_ON, choke = event->type == CLAP_EVENT_NOTE_CHOKE;
		int minimum = on ? 0 : -1;
		if (e->port_index < minimum || e->port_index > 0 || e->channel < minimum || e->channel > 15 ||
		    e->key < minimum || e->key > 127 || e->note_id < -1 ||
		    (!choke && (!isfinite(e->velocity) || e->velocity < 0 || e->velocity > 1)))
			return false;
		uint8_t velocity = choke ? 0 : lround(e->velocity * 127);
		if (on) {
			uint8_t data[] = {0x90 | e->channel, e->key, velocity ? velocity : 1};
			return midi_output(c, event->time, data, 3, e->note_id);
		}
		int concrete = e->channel >= 0 && e->key >= 0 && e->note_id == -1;
		if (concrete && dsp_midi_output(c->dsp, event->time, (uint8_t[]){0x80 | e->channel, e->key, velocity}, 3))
			return false;
		for (unsigned i = 0; i < c->nheld;) {
			ClapNote *note = c->held + i;
			if ((e->note_id >= 0 && e->note_id != note->id) || (e->channel >= 0 && e->channel != note->channel) ||
			    (e->key >= 0 && e->key != note->key)) {
				++i;
				continue;
			}
			uint8_t data[] = {0x80 | note->channel, note->key, velocity};
			if (!concrete && dsp_midi_output(c->dsp, event->time, data, 3))
				return false;
			memmove(note, note + 1, (--c->nheld - i) * sizeof(*note));
		}
		return true;
	}
	return event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || event->type == CLAP_EVENT_PARAM_GESTURE_END ||
	    event->type == CLAP_EVENT_NOTE_END;
}
static ClapEvent *event_new(Clap *c, uint16_t type, uint32_t size) {
	if (c->count == c->capacity) {
		atomic_fetch_or(&c->requests, CLAP_HOST_ERROR);
		return NULL;
	}
	ClapEvent *event = c->events + c->count++;
	memset(event, 0, sizeof(*event));
	event->header = (clap_event_header_t){.size = size, .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = type};
	return event;
}
static void set(DSP *dsp, size_t index, double value) {
	Clap *c = dsp->instance;
	ClapEvent *e = c->pending[index] ? c->events + c->pending[index] - 1
	                                 : event_new(c, CLAP_EVENT_PARAM_VALUE, sizeof(clap_event_param_value_t));
	if (!e)
		return;
	c->pending[index] = e - c->events + 1;
	e->parameter.param_id = c->info[index].id;
	e->parameter.cookie = c->info[index].cookie;
	e->parameter.note_id = -1;
	e->parameter.port_index = e->parameter.channel = e->parameter.key = -1;
	e->parameter.value = value;
}
static void clear_events(Clap *c) {
	for (uint32_t i = 0; i < c->count; ++i)
		if (c->events[i].header.type == CLAP_EVENT_PARAM_VALUE)
			c->pending[parameter_index(c, c->events[i].parameter.param_id)] = 0;
	c->count = c->sysex_used = 0;
}
static void flush_controls(DSP *dsp) {
	Clap *c = dsp->instance;
	if (!c->params)
		return;
	for (uint32_t i = 0; i < c->count; ++i)
		if (c->events[i].header.type != CLAP_EVENT_PARAM_VALUE)
			return;
		else if (c->active &&
		    (c->info[parameter_index(c, c->events[i].parameter.param_id)].flags & CLAP_PARAM_REQUIRES_PROCESS))
			return;
	unsigned requests = atomic_fetch_and(&c->requests, ~CLAP_HOST_FLUSH);
	if (!c->count && !(requests & CLAP_HOST_FLUSH))
		return;
	audio_owner = c->active ? c : NULL;
	c->params->flush(c->plugin, &c->in, &c->out);
	audio_owner = NULL;
	clear_events(c);
}
static void reset(DSP *dsp) {
	Clap *c = dsp->instance;
	if (!c->active) {
		atomic_fetch_or(&c->requests, CLAP_HOST_ERROR);
		return;
	}
	// A previous seek may have queued notes without processing them yet.
	uint32_t count = 0;
	for (uint32_t i = 0; i < c->count; ++i)
		if (c->events[i].header.type == CLAP_EVENT_PARAM_VALUE) {
			c->events[count++] = c->events[i];
			c->pending[parameter_index(c, c->events[i].parameter.param_id)] = count;
		}
	c->count = count;
	c->sysex_used = 0;
	flush_controls(dsp); // Controls requiring process() remain queued for the next block.
	audio_owner = c;
	c->plugin->reset(c->plugin);
	audio_owner = NULL;
	c->steady = 0;
	c->nheld = 0;
}
static void midi(DSP *dsp, size_t bus, const uint8_t *message, size_t size) {
	Clap *c = dsp->instance;
	if (c->dialect == CLAP_NOTE_DIALECT_CLAP) {
		if ((message[0] & 0xf0) != 0x90 && (message[0] & 0xf0) != 0x80)
			return;
		int on = (message[0] & 0xf0) == 0x90 && message[2];
		ClapEvent *e = event_new(c, on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF, sizeof(clap_event_note_t));
		if (!e)
			return;
		e->note.note_id = -1;
		e->note.port_index = bus;
		e->note.channel = message[0] & 15;
		e->note.key = message[1];
		e->note.velocity = message[2] / 127.0;
	} else if (message[0] == 0xf0) {
		if (size > sizeof(c->sysex) - c->sysex_used) {
			atomic_fetch_or(&c->requests, CLAP_HOST_ERROR);
			return;
		}
		ClapEvent *e = event_new(c, CLAP_EVENT_MIDI_SYSEX, sizeof(clap_event_midi_sysex_t));
		if (!e)
			return;
		e->sysex.port_index = bus;
		e->sysex.buffer = c->sysex + c->sysex_used;
		e->sysex.size = size;
		memcpy(c->sysex + c->sysex_used, message, size);
		c->sysex_used += size;
	} else {
		ClapEvent *e = event_new(c, CLAP_EVENT_MIDI, sizeof(clap_event_midi_t));
		if (!e)
			return;
		e->midi.port_index = bus;
		memcpy(e->midi.data, message, size);
	}
}
static int process(DSP *dsp, const float **inputs, float **outputs, size_t frames, const ProcessTime *time) {
	Clap *c = dsp->instance;
	if (!c->active || (atomic_load(&c->requests) & CLAP_HOST_ERROR))
		return -1;
	audio_owner = c;
	if (!c->processing) {
		if (!c->plugin->start_processing(c->plugin)) {
			audio_owner = NULL;
			return -1;
		}
		c->processing = 1;
	}
	for (uint32_t i = 0; i < c->input_slots; ++i)
		c->input_channels[i] = inputs && inputs[i] ? (float *)inputs[i] : c->silence;
	for (int i = 0; i < dsp->config.output; ++i)
		c->output_channels[i] = outputs[i];
	for (uint32_t i = 0; i < c->ninputs; ++i)
		c->inputs[i].constant_mask = 0;
	for (uint32_t i = 0; i < c->noutputs; ++i)
		c->outputs[i].constant_mask = 0;
	clap_event_transport_t transport = {
	    .header = {.size = sizeof(transport), .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = CLAP_EVENT_TRANSPORT}};
	if (time) {
		transport.flags = CLAP_TRANSPORT_HAS_TEMPO;
		if (time->playing)
			transport.flags |= CLAP_TRANSPORT_IS_PLAYING;
		transport.tempo = time->bpm > 0 ? time->bpm : 120;
		double beats = time->beat * CLAP_BEATTIME_FACTOR;
		double seconds = (double)time->frame / c->rate * CLAP_SECTIME_FACTOR;
		if (beats >= 0 && beats < 0x1p63) {
			transport.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
			transport.song_pos_beats = beats;
		}
		if (seconds >= 0 && seconds < 0x1p63) {
			transport.flags |= CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
			transport.song_pos_seconds = seconds;
		}
	}
	clap_process_t block = {.steady_time = c->steady,
	    .frames_count = frames,
	    .transport = time ? &transport : NULL,
	    .audio_inputs = c->inputs,
	    .audio_inputs_count = c->ninputs,
	    .audio_outputs = c->outputs,
	    .audio_outputs_count = c->noutputs,
	    .in_events = &c->in,
	    .out_events = &c->out};
	int status = c->plugin->process(c->plugin, &block);
	c->steady += frames;
	clear_events(c);
	audio_owner = NULL;
	return status == CLAP_PROCESS_ERROR || (atomic_load(&c->requests) & CLAP_HOST_ERROR) ? -1 : 0;
}
static int poll_instance(DSP *dsp) {
	Clap *c = dsp->instance;
	unsigned requests = atomic_fetch_and(&c->requests, ~(CLAP_HOST_CALLBACK | CLAP_HOST_VALUES));
	if (requests & CLAP_HOST_CALLBACK)
		c->plugin->on_main_thread(c->plugin);
	if ((requests & CLAP_HOST_VALUES) && c->params)
		for (uint32_t i = 0; i < c->nparams; ++i) {
			double value;
			if (c->params->get_value(c->plugin, c->info[i].id, &value) && clap_value(c->info + i, &value))
				atomic_store(&dsp->values[i], value);
		}
	ClapTimer timers[64];
	unsigned ntimers = c->ntimers;
	memcpy(timers, c->timers, ntimers * sizeof(*timers));
	uint64_t now = host_clock();
	if (c->timer)
		for (unsigned i = 0; i < ntimers; ++i)
			if (timers[i].next <= now) {
				for (unsigned j = 0; j < c->ntimers; ++j)
					if (c->timers[j].id == timers[i].id) {
						c->timers[j].next = now + c->timers[j].period;
						c->timer->on_timer(c->plugin, timers[i].id);
						break;
					}
			}
	clap_platform_poll(c);
	requests = atomic_load(&c->requests);
	return requests & CLAP_HOST_ERROR ? -1 : !!(requests & CLAP_HOST_RESTART);
}
static int activate(Clap *c) {
	if (!c->plugin->activate(c->plugin, c->rate, 1, c->block))
		return -1;
	c->active = 1;
	return 0;
}
static int compatible(Clap *c) {
	const PluginConfig *config = &c->dsp->config;
	int input, output, midi;
	if (clap_layout(c, &input, &output, &midi) || input != config->input || output != config->output ||
	    midi != config->midi || c->midi_output != config->midi_output ||
	    (c->params ? c->params->count(c->plugin) : 0) != (uint32_t)config->nparams)
		return -1;
	for (uint32_t i = 0; i < c->nparams; ++i) {
		clap_param_info_t info;
		if (!c->params->get_info(c->plugin, i, &info) || info.id != c->info[i].id ||
		    info.min_value != c->info[i].min_value || info.max_value != c->info[i].max_value ||
		    ((info.flags ^ c->info[i].flags) & ~(CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_PERIODIC)))
			return -1;
		c->info[i] = info;
	}
	// Rescanning may replace cookies while notes and controls are still queued.
	for (uint32_t i = 0; i < c->count; ++i)
		if (c->events[i].header.type == CLAP_EVENT_PARAM_VALUE) {
			clap_event_param_value_t *p = &c->events[i].parameter;
			p->cookie = c->info[parameter_index(c, p->param_id)].cookie;
		}
	return 0;
}
static uint32_t latency(DSP *dsp) {
	Clap *c = dsp->instance;
	return c->active && c->latency ? c->latency->get(c->plugin) : 0;
}
static int restart(DSP *dsp) {
	Clap *c = dsp->instance;
	if (!(atomic_load(&c->requests) & CLAP_HOST_RESTART))
		return 0;
	flush_controls(dsp);
	suspend(c);
	atomic_fetch_and(&c->requests, ~CLAP_HOST_RESTART);
	if (compatible(c) || activate(c)) {
		fprintf(stderr, "[CLAP] Plugin configuration changed; prepare the score again\n");
		atomic_fetch_or(&c->requests, CLAP_HOST_ERROR);
		return -1;
	}
	return 0;
}
static int flush_inactive_controls(DSP *dsp) {
	Clap *c = dsp->instance;
	if (!c->params || !c->count)
		return 0;
	// Inactive plugins accept REQUIRES_PROCESS controls via flush(). Keep queued notes for playback.
	ClapEvent *notes = malloc(c->count * sizeof(*notes));
	if (!notes)
		return -1;
	uint32_t count = 0, parameters = 0;
	for (uint32_t i = 0; i < c->count; ++i)
		if (c->events[i].header.type == CLAP_EVENT_PARAM_VALUE)
			c->events[parameters++] = c->events[i];
		else
			notes[count++] = c->events[i];
	unsigned used = c->sysex_used;
	c->count = parameters;
	flush_controls(dsp);
	c->sysex_used = used;
	memcpy(c->events, notes, count * sizeof(*notes));
	c->count = count;
	free(notes);
	return atomic_load(&c->requests) & CLAP_HOST_ERROR ? -1 : 0;
}
static int offline(DSP *dsp, int enabled) {
	Clap *c = dsp->instance;
	if (atomic_load(&c->requests) & CLAP_HOST_ERROR)
		return -1;
	if (!c->render || c->offline == enabled)
		return 0;
	if (enabled && c->render->has_hard_realtime_requirement(c->plugin))
		return -1;
	flush_controls(dsp);
	suspend(c);
	int ok = !compatible(c) && !flush_inactive_controls(dsp) &&
	    c->render->set(c->plugin, enabled ? CLAP_RENDER_OFFLINE : CLAP_RENDER_REALTIME);
	if (ok)
		c->offline = enabled;
	atomic_fetch_and(&c->requests, ~CLAP_HOST_RESTART);
	if (compatible(c) || activate(c)) {
		atomic_fetch_or(&c->requests, CLAP_HOST_ERROR);
		return -1;
	}
	return ok && !(atomic_load(&c->requests) & CLAP_HOST_ERROR) ? 0 : -1;
}
static int64_t stream_write(const clap_ostream_t *stream, const void *data, uint64_t size) {
	return size > STATE_LIMIT || state_write(stream->ctx, data, size) ? -1 : (int64_t)size;
}
static int64_t stream_read(const clap_istream_t *stream, void *data, uint64_t size) {
	return state_read(stream->ctx, data, size > STATE_LIMIT ? STATE_LIMIT : (size_t)size);
}
static int state(DSP *dsp, State *buffer, int write) {
	Clap *c = dsp->instance;
	if (!c->state || !c->state->save || !c->state->load)
		return STATE_UNSUPPORTED;
	flush_controls(dsp);
	suspend(c);
	int flushed = !compatible(c) && !flush_inactive_controls(dsp);
	clap_ostream_t out = {.ctx = buffer, .write = stream_write};
	clap_istream_t in = {.ctx = buffer, .read = stream_read};
	int ok = flushed && (write ? c->state->save(c->plugin, &out) : c->state->load(c->plugin, &in));
	atomic_fetch_and(&c->requests, ~CLAP_HOST_RESTART);
	if (compatible(c) || activate(c)) {
		ok = 0;
		atomic_fetch_or(&c->requests, CLAP_HOST_ERROR);
	}
	atomic_fetch_or(&c->requests, CLAP_HOST_VALUES);
	return ok ? 0 : -1;
}
static void close_instance(DSP *dsp) {
	Clap *c = dsp->instance;
	if (c) {
		clap_destroy(c);
		free(c);
	}
}
static const DspOps ops = {.close = close_instance,
    .set = set,
    .reset = reset,
    .midi = midi,
    .process = process,
    .sync = flush_controls,
    .poll = poll_instance,
    .restart = restart,
    .offline = offline,
    .state = state,
    .latency = latency};
int clap_open(DSP *dsp, unsigned rate, size_t capacity) {
	Clap *c = calloc(1, sizeof(*c));
	if (!c)
		return -1;
	dsp->instance = c;
	dsp->ops = &ops;
	c->dsp = dsp;
	if (clap_create(c, dsp->module, dsp->config.id))
		return -1;
	c->rate = rate;
	c->block = capacity;
	if (compatible(c))
		return -1;
	c->inputs = c->ninputs ? calloc(c->ninputs, sizeof(*c->inputs)) : NULL;
	c->outputs = calloc(c->noutputs, sizeof(*c->outputs));
	c->input_channels = c->input_slots ? calloc(c->input_slots, sizeof(float *)) : NULL;
	c->output_channels = calloc(c->output_slots, sizeof(float *));
	c->silence = calloc(capacity, sizeof(float));
	c->scratch = calloc(c->output_slots * capacity, sizeof(float));
	if ((c->ninputs && !c->inputs) || (c->noutputs && !c->outputs) || (c->input_slots && !c->input_channels) ||
	    (c->output_slots && !c->output_channels) || !c->silence || (c->output_slots && !c->scratch))
		return -1;
	for (uint32_t i = 0; i < c->output_slots; ++i)
		c->output_channels[i] = c->scratch + i * capacity;
	for (int in = 0; in < 2; ++in) {
		uint32_t offset = 0;
		for (uint32_t i = 0; i < (in ? c->ninputs : c->noutputs); ++i) {
			clap_audio_port_info_t p;
			if (!c->audio->get(c->plugin, i, in, &p))
				return -1;
			(in ? c->inputs : c->outputs)[i] = (clap_audio_buffer_t){
			    .channel_count = p.channel_count, .data32 = (in ? c->input_channels : c->output_channels) + offset};
			offset += p.channel_count;
		}
	}
	for (uint32_t i = 0; i < c->nparams; ++i) {
		const Parameter *p = dsp->config.parameters + i;
		const clap_param_info_t *actual = c->info + i;
		unsigned flags = (actual->flags & CLAP_PARAM_IS_READONLY ? PARAM_OUTPUT : 0) |
		    (actual->flags & CLAP_PARAM_IS_STEPPED ? PARAM_INTEGER : 0) |
		    (actual->flags & CLAP_PARAM_IS_AUTOMATABLE ? PARAM_AUTOMATABLE : 0);
		if (p->id != actual->id || p->minimum != actual->min_value || p->maximum != actual->max_value ||
		    p->flags != flags)
			return -1;
	}
	c->capacity = c->nparams + 512;
	c->events = calloc(c->capacity, sizeof(ClapEvent));
	c->pending = c->nparams ? calloc(c->nparams, sizeof(*c->pending)) : NULL;
	if (!c->events || (c->nparams && !c->pending))
		return -1;
	c->in = (clap_input_events_t){c, event_count, event_get};
	c->out = (clap_output_events_t){c, event_push};
	for (uint32_t i = 0; i < c->nparams; ++i)
		if (!(dsp->config.parameters[i].flags & PARAM_OUTPUT))
			set(dsp, i, dsp->config.parameters[i].value);
	flush_controls(dsp);
	atomic_fetch_and(&c->requests, ~CLAP_HOST_RESTART);
	if (activate(c))
		return -1;
	reset(dsp);
	return 0;
}
