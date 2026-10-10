#ifndef DSP_H
#define DSP_H
#include <stddef.h>
#include <stdint.h>

enum { MAX_PARAMS = 65536, MAX_INPUTS = 8 };
enum { PLUGIN_PERONE, PLUGIN_CLAP, PLUGIN_LV2, PLUGIN_FORMATS };
enum { PARAM_OUTPUT = 1, PARAM_INTEGER = 2, PARAM_AUTOMATABLE = 4 };

typedef struct {
	uint32_t id;
	unsigned flags;
	double value, minimum, maximum;
} Parameter;
// Zero when the backend cannot route plugin messages.
extern const size_t dsp_max_message;

// Numeric setup supplied by Janet. No product metadata or platform handles.
typedef struct {
	int input, output;              // Channel counts of the main audio buses.
	int midi, input_offset, inputs; // MIDI bus index; main input offset and flattened input count.
	int nparams, format;
	char *id; // Format-native plugin identity; NULL for Perone.
	Parameter *parameters;
	size_t to_ui, to_dsp; // Maximum message sizes from product.json.
} PluginConfig;

typedef struct {
	uint64_t frame;
	double bpm, beat;
	int playing, offline;
} ProcessTime;

typedef struct DSP DSP;
typedef struct Module Module;
typedef struct {
	Module *head;
} Modules;

// A DAW-owned cache. Instances retain their module independently of the cache.
void modules_free(Modules *modules);

// Optional cache retains format resources for reuse. A failed open releases instance resources and returns NULL.
DSP *open_dsp(Modules *modules, const char *path, const PluginConfig *config, unsigned sample_rate, size_t capacity);
void close_dsp(DSP *dsp); // NULL is allowed.
void set_dsp(DSP *dsp, size_t parameter, double value);
void reset_dsp(DSP *dsp);
void midi_dsp(DSP *dsp, size_t bus, const uint8_t *message);
// Apply pending host edits before rendering either instance of a mono pair. NULL is allowed.
void sync_dsp(DSP *dsp, DSP *paired);
void idle_dsp(DSP *dsp, const ProcessTime *time); // Service messages with audio stopped, without advancing time.
// Planar buffers belong to the caller; NULL optional inputs remain disconnected.
int process_dsp(DSP *dsp, const float **inputs, float **outputs, size_t frames, const ProcessTime *time);
// Control thread only; the caller must stop audio before restart or state access.
int poll_dsp(DSP *dsp);
uint32_t latency_dsp(DSP *dsp);
int restart_dsp(DSP *dsp);
int state_dsp(DSP *dsp, const char *path, int write);
#endif
