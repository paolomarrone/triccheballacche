#ifndef LOADER_H
#define LOADER_H

#include "module.h"

TibiaModule* tibia_loader_load(const char *path);
void         tibia_loader_unload(TibiaModule *module);

#endif