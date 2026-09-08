#ifndef MODULE_H
#define MODULE_H
#include "tibia/tibia.h"
typedef struct {
	void *handle;
	const tibia_api *api;
	int input, output, midi; /* Audio channel counts; MIDI is the JSON bus index, or -1. */
} TibiaModule;
#endif
