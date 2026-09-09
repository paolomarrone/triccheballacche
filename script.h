#ifndef SCRIPT_H
#define SCRIPT_H
#include "engine.h"
#include "janet.h"
// Synchronous setup only. script_env starts Janet; its caller must close it.
JanetTable *script_env(void);
void script_config(Janet layout, Janet defaults, PluginConfig *config);
// These helpers start and close Janet themselves; call without an active VM.
int read_bundle(const char *path, char **binary, PluginConfig *config);
int open_bundle(Engine *engine, const char *path);
#endif
