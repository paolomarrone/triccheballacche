#ifndef HOST_CLAP_H
#define HOST_CLAP_H
#include "dsp_internal.h"
#include "json_write.h"
#include <clap/clap.h>

enum {
	CLAP_HOST_CALLBACK = 1,
	CLAP_HOST_RESTART = 2,
	CLAP_HOST_FLUSH = 4,
	CLAP_HOST_VALUES = 8,
	CLAP_HOST_CLOSED = 16,
	CLAP_HOST_ERROR = 32
};
typedef union {
	clap_event_header_t header;
	clap_event_param_value_t parameter;
	clap_event_note_t note;
	clap_event_midi_t midi;
} ClapEvent;
typedef struct {
	clap_id id;
	int index;
} ClapParameter;
typedef struct {
	clap_id id;
	uint32_t period;
	uint64_t next;
} ClapTimer;
typedef struct ClapPlatform ClapPlatform;
typedef struct Clap {
	DSP *dsp;
	clap_host_t host;
	const clap_plugin_t *plugin;
	const clap_plugin_params_t *params;
	const clap_plugin_audio_ports_t *audio;
	const clap_plugin_note_ports_t *notes;
	const clap_plugin_state_t *state;
	const clap_plugin_latency_t *latency;
	const clap_plugin_gui_t *gui;
	const clap_plugin_timer_support_t *timer;
	clap_param_info_t *info;
	ClapParameter *by_id;
	uint32_t nparams, dialect;
	uint32_t ninputs, noutputs, input_slots, output_slots;
	clap_audio_buffer_t *inputs, *outputs;
	float **input_channels, **output_channels, *silence, *scratch;
	ClapEvent *events;
	uint32_t *pending; // Queued parameter event index + 1; zero means none.
	uint32_t count, capacity;
	clap_input_events_t in;
	clap_output_events_t out;
	const void *main_thread;
	atomic_uint requests;
	int active, processing;
	unsigned rate, block;
	int64_t steady;
	ClapTimer timers[64];
	ClapPlatform *platform;
	unsigned ntimers, next_timer;
	void *window;
	bool (*resize)(void *window, uint32_t width, uint32_t height);
	void (*show)(void *window, int visible);
} Clap;
// Shared by the scanner and runtime; all lifecycle functions use the caller's main thread.
int clap_create(Clap *c, Module *module, const char *id);
void clap_destroy(Clap *c);
int clap_metadata(Clap *c, Json *json, const char *path);
int clap_scan(Json *json, const char *path);
int clap_layout(Clap *c, int *input, int *output, int *midi);
int clap_parameters(Clap *c);
bool clap_value(const clap_param_info_t *parameter, double *value);
const clap_plugin_entry_t *clap_module_entry(Module *module);
extern const char *const clap_window_api;

// Optional platform extensions; serviced on the plugin's main thread.
int clap_platform_open(Clap *c);
void clap_platform_close(Clap *c);
void clap_platform_poll(Clap *c);
const void *clap_platform_extension(const char *id);
#endif
