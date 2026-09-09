#ifndef MODULE_H
#define MODULE_H
#include "perone.h"

enum { MAX_PARAMS = 64, MAX_INPUTS = 8 };

// Numeric setup supplied by the host, after reading product.json in Janet.
typedef struct {
	int input, output;              // Channel counts of the main audio buses.
	int midi, input_offset, inputs; // MIDI bus index; main input offset and total flattened input channels.
	int nparams;
	uint64_t outputs;
	float defaults[MAX_PARAMS]; // Initial values, including host overrides; not live DSP values.
} PluginConfig;

typedef struct {
	void *handle;
	const perone_api *api;
	PluginConfig config;
	char *bindir, *datadir;
} Module;
#endif
