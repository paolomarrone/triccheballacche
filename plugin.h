#ifndef PLUGIN_H
#define PLUGIN_H
#include "loader.h"

enum { DEFAULT_SAMPLE_RATE = 44100, BLOCK = 512 };

// Product metadata, read once by Janet and owned independently of any GUI.
typedef struct {
	char *name, *product, *ui;
	int resizable;
	float minimum[MAX_PARAMS], maximum[MAX_PARAMS];
	uint64_t integers;
} PluginInfo;
void plugin_info_free(PluginInfo *info);
int plugin_parameter(const PluginConfig *config, const PluginInfo *info, size_t index, float *value);

typedef struct {
	DSP *dsp;
	Modules *modules; // Optional borrowed cache; NULL gives this instance a private module reference.
	PluginConfig config;
} Plugin;

void close_plugin(Plugin *e);
int plugin_config_valid(const PluginConfig *config);
int open_plugin(Plugin *e, const char *path, const PluginConfig *config, unsigned sample_rate);
// Interleaved buffers sized by module input/output channels; NULL input is silence.
void render_plugin(Plugin *e, float *out, const float *in, size_t frames);
#endif
