#ifndef MODULE_H
#define MODULE_H
#include "dsp.h"

typedef struct {
	void *(*open)(const char *path);
	void (*close)(void *data);
} ModuleOps;
extern const ModuleOps perone_module_ops, clap_module_ops, lv2_module_ops, vst3_module_ops;

// Cache and instances hold independent references. Paths are canonicalized before lookup.
Module *module_open(Modules *cache, const char *path, int format);
void module_close(Module *module);
void *module_data(Module *module); // Format-owned payload; valid until the last reference closes.
const char *module_path(Module *module);
#endif
