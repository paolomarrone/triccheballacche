#ifndef MODULE_H
#define MODULE_H
#include "loader.h"
#include "perone.h"
#include <stdatomic.h>

enum { MESSAGE_SLOTS = 16, MAX_MESSAGE = 4096 };

// One producer and one consumer. Storage is allocated before processing.
typedef struct {
	atomic_uint read, write;
	size_t sizes[MESSAGE_SLOTS], limit;
	unsigned char *data;
} Messages;

int message_push(Messages *queue, size_t size, const void *data);
int message_pop(Messages *queue, size_t *size, void *data);

// One UI consumer per DSP. The DSP must outlive every attachment.
void watch_dsp(DSP *dsp, int watching);
// Validated input values only. Latest value wins; sync_dsp applies parameters in index order,
// then messages in FIFO order. There is no combined parameter/message event order.
void edit_dsp(DSP *dsp, size_t parameter, float value);
int send_dsp(DSP *dsp, size_t size, const void *data); // 0 accepted, -1 full/invalid.
// 1 current value, 0 while an edit awaits audio-thread acknowledgement.
int read_dsp(DSP *dsp, size_t parameter, float *value);
int receive_dsp(DSP *dsp, size_t *size, void *data); // 1 message, 0 empty, -1 overflow.

// Native loader/UI internals. Only the audio thread calls the DSP while playing.
struct DSP {
	void *handle, *instance, *memory;
	const perone_api *api;
	char *bindir, *datadir;
	int initialized;
	PluginConfig config;
	atomic_int viewing, overflow, pending;
	atomic_uint requested[MAX_PARAMS], applied[MAX_PARAMS];
	_Atomic float wanted[MAX_PARAMS], values[MAX_PARAMS];
	Messages to_ui, to_dsp;
};
#endif
