#ifndef TRICCHE_LOADER_H
#define TRICCHE_LOADER_H

#include "tricche_tibia"

tibia_plugin* tricche_load_tibia   (const char* path);
void          tricche_unload_tibia (tibia_plugin* plugin);

#endif
