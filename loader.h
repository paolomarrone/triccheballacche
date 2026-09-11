#ifndef LOADER_H
#define LOADER_H
#include <stddef.h>
#include <stdint.h>

enum { MAX_PARAMS = 64, MAX_INPUTS = 8 };

// Numeric setup supplied by Janet. No product metadata or platform handles.
typedef struct {
	int input, output;              // Channel counts of the main audio buses.
	int midi, input_offset, inputs; // MIDI bus index; main input offset and flattened input count.
	int nparams;
	uint64_t outputs;
	float defaults[MAX_PARAMS]; // Initial values, including host overrides.
} PluginConfig;

typedef struct DSP DSP;

// A successful open owns its resources; a failed open releases everything and returns NULL.
DSP *open_dsp(const char *path, const PluginConfig *config, unsigned sample_rate, size_t capacity);
void close_dsp(DSP *dsp); // NULL is allowed.
void set_dsp(DSP *dsp, size_t parameter, float value);
void reset_dsp(DSP *dsp);
void midi_dsp(DSP *dsp, size_t bus, const uint8_t *message);
// Planar buffers belong to the caller; NULL optional inputs remain disconnected.
void process_dsp(DSP *dsp, const float **inputs, float **outputs, size_t frames);
#endif
