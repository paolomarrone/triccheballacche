#ifndef DSP_INTERNAL_H
#define DSP_INTERNAL_H
#include "dsp.h"
#include "module.h"
#include <stdatomic.h>

enum { MESSAGE_SLOTS = 64, MAX_MESSAGE = 4096 };
typedef struct {
	atomic_uint read, write;
	size_t sizes[MESSAGE_SLOTS], limit;
	unsigned char *data;
} Messages;
int message_push(Messages *queue, size_t size, const void *data);
int message_pop(Messages *queue, size_t *size, void *data);
void watch_dsp(DSP *dsp, int watching);
void edit_dsp(DSP *dsp, size_t parameter, double value);
int send_dsp(DSP *dsp, size_t size, const void *data);
int read_dsp(DSP *dsp, size_t parameter, double *value);
int receive_dsp(DSP *dsp, size_t *size, void *data);
void dsp_message(void *dsp, size_t size, const void *data);

typedef struct {
	void (*close)(DSP *);
	void (*set)(DSP *, size_t, double);
	void (*reset)(DSP *);
	void (*midi)(DSP *, size_t, const uint8_t *);
	int (*process)(DSP *, const float **, float **, size_t, const ProcessTime *);
	void (*sync)(DSP *);                      // Audio role, also while the device is stopped.
	void (*idle)(DSP *, const ProcessTime *); // Control thread, no concurrent rendering.
	void (*message)(DSP *, size_t, const void *);
	int (*poll)(DSP *);                     // Main thread. 1 requests a quiescent restart, -1 reports an error.
	int (*restart)(DSP *);                  // Main thread, no concurrent audio calls.
	uint32_t (*latency)(DSP *);             // Main thread, active.
	int (*state)(DSP *, const char *, int); // Main thread, stopped; save if write != 0.
} DspOps;
struct DSP {
	const DspOps *ops;
	Module *module;
	void *instance;
	PluginConfig config; // Borrows the Plugin's owned parameter table.
	atomic_int viewing, overflow, pending;
	atomic_uint *requested, *applied;
	_Atomic double *wanted, *values;
	Messages to_ui, to_dsp;
};

int perone_open(DSP *dsp, unsigned sample_rate, size_t capacity);
int clap_open(DSP *dsp, unsigned sample_rate, size_t capacity);
int lv2_open(DSP *dsp, unsigned sample_rate, size_t capacity);
#endif
