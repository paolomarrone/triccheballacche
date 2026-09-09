#ifndef LOADER_H
#define LOADER_H
#include "module.h"
Module *load_module(const char *path, const PluginConfig *config);
void unload_module(Module *module);
#endif
