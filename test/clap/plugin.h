#ifndef TEST_CLAP_PLUGIN_H
#define TEST_CLAP_PLUGIN_H
#include <clap/clap.h>
#include <clap_platform.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
enum { PARAMETERS = 130 };
typedef struct {
	clap_plugin_t plugin;
	const clap_host_t *host;
	const clap_host_thread_check_t *threads;
	const clap_host_params_t *host_params;
	const clap_host_timer_support_t *host_timer;
	_Atomic double values[PARAMETERS];
	atomic_int edited;
	int effect, active, processing, held, offline;
	unsigned limit, latency, position, generation;
	float delay[2][7];
	FixtureDisplay *display;
	FixtureWindow window;
	clap_id timer;
	double private_state;
} Plugin;
static inline Plugin *self(const clap_plugin_t *p) {
	return p->plugin_data;
}
static inline void main_thread(Plugin *p) {
	assert(p->threads->is_main_thread(p->host));
}
static inline void audio_thread(Plugin *p) {
	assert(p->threads->is_audio_thread(p->host));
}
bool gui_supported(const clap_plugin_t *plugin, const char *api, bool floating);
bool gui_create(const clap_plugin_t *plugin, const char *api, bool floating);
void gui_destroy(const clap_plugin_t *plugin);
bool gui_size(const clap_plugin_t *plugin, uint32_t *w, uint32_t *h);
bool gui_resizable(const clap_plugin_t *plugin);
bool gui_parent(const clap_plugin_t *plugin, const clap_window_t *window);
bool gui_show(const clap_plugin_t *plugin);
bool gui_hide(const clap_plugin_t *plugin);
void timer(const clap_plugin_t *plugin, clap_id id);
#endif
