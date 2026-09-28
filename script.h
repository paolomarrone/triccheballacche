#ifndef SCRIPT_H
#define SCRIPT_H
#include "plugin.h"
#include "janet.h"
// Synchronous setup only. script_env starts Janet; its caller must close it.
JanetTable *script_env(void);
void script_trace(JanetTable *env);
void script_info(Janet bundle, Janet encoded, PluginInfo *info);
void script_config(Janet layout, Janet defaults, PluginConfig *config);
// These helpers start and close Janet themselves; call without an active VM.
int read_bundle(const char *path, char **binary, PluginConfig *config, PluginInfo *info);
int open_bundle(Plugin *plugin, const char *path, unsigned sample_rate);
#endif
