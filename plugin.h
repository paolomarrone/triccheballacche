#ifndef PLUGIN_H
#define PLUGIN_H
#include "dsp.h"

enum { DEFAULT_SAMPLE_RATE = 44100, BLOCK = 512 };

// Product metadata, read once by Janet and owned independently of any GUI.
typedef struct {
	char *name, *product, *ui;
	int resizable;
} PluginInfo;
void plugin_info_free(PluginInfo *info);
int plugin_parameter(const PluginConfig *config, size_t index, double *value);
int plugin_config_copy(PluginConfig *out, const PluginConfig *source);
void plugin_config_free(PluginConfig *config);

typedef struct {
	DSP *dsp;
	Modules *modules; // Optional borrowed cache; NULL gives this instance a private module reference.
	PluginConfig config;
	ProcessTime time;
} Plugin;

void close_plugin(Plugin *e);
int plugin_config_valid(const PluginConfig *config);
int open_plugin(Plugin *e, const char *path, const PluginConfig *config, unsigned sample_rate);
// Interleaved buffers sized by module input/output channels; NULL input is silence.
int render_plugin(Plugin *e, float *out, const float *in, size_t frames);
#endif
