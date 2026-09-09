/* Perone ABI v2. GPL-3.0-or-later. */
#ifndef PERONE_H
#define PERONE_H
#include <stddef.h>
#include <stdint.h>

#define PERONE_ABI_VERSION 2

typedef struct {
	void *handle;
	const char *(*get_bindir)(void *handle);
	const char *(*get_datadir)(void *handle);
	void (*msg_write)(void *handle, size_t size, const void *data);
} perone_callbacks;

typedef struct {
	void *handle;
	void (*lock)(void *handle);
	void (*unlock)(void *handle);
	int (*write)(void *handle, const char *data, size_t length);
	void (*set_parameter)(void *handle, size_t index, float value);
} perone_state_callbacks;

#define PERONE_TRANSPORT_PLAYING        ((uint32_t)1)
#define PERONE_TRANSPORT_SPEED          ((uint32_t)(1 << 1))
#define PERONE_TRANSPORT_BPM            ((uint32_t)(1 << 2))
#define PERONE_TRANSPORT_QUARTER        ((uint32_t)(1 << 3))
#define PERONE_TRANSPORT_BEAT           ((uint32_t)(1 << 4))
#define PERONE_TRANSPORT_TIME_SIG_NUM   ((uint32_t)(1 << 5))
#define PERONE_TRANSPORT_TIME_SIG_DENOM ((uint32_t)(1 << 6))
#define PERONE_TRANSPORT_BAR            ((uint32_t)(1 << 7))
#define PERONE_TRANSPORT_BAR_BEAT       ((uint32_t)(1 << 8))

typedef struct {
	uint32_t changed;
	uint32_t valid;
	char playing;
	float speed;
	float bpm;
	double quarter;
	double beat;
	float time_sig_num;
	uint32_t time_sig_denom;
	uint64_t bar;
	float bar_beat;
} perone_transport;

typedef struct {
	void *(*alloc)(void);
	void (*free)(void *instance);
	int (*init)(void *instance, const perone_callbacks *callbacks);
	void (*fini)(void *instance);
	void (*set_sample_rate)(void *instance, float sample_rate);
	size_t (*mem_req)(void *instance);
	void (*mem_set)(void *instance, void *mem);
	void (*reset)(void *instance);
	void (*process)(void *instance, const float **inputs, float **outputs, size_t frames);
	void (*set_parameter)(void *instance, size_t index, float value);
	float (*get_parameter)(void *instance, size_t index);
	void (*midi_msg_in)(void *instance, size_t bus, const uint8_t *data);
	void (*set_transport)(void *instance, const perone_transport *transport);
	void (*msg_in)(void *instance, size_t size, const void *data);
	int (*state_save)(void *instance, const perone_state_callbacks *callbacks, float last_sample_rate);
	int (*state_load)(const perone_state_callbacks *callbacks, float cur_sample_rate, const char *data, size_t length);
} perone_api;

/* alloc/free manage aligned instance storage only; alloc returns NULL on failure.
 * All other entries forward plugin_* calls, arguments and return values.
 * init/fini own C++ member lifetimes. free(NULL) is allowed; free never calls fini.
 * The host sets defaults and sample rate, supplies DSP memory, then calls reset.
 * The host owns that memory and handles reconfiguration and synchronization.
 * Callback structures have a fixed layout. init supplies format = "perone".
 * Pass valid structures; callback functions and handles must outlive their use.
 * Optional entries are NULL when their source API capability is not declared.
 * Indices, units and channel order follow product.json; arguments are unfiltered.
 * Messaging is synchronous. The host handles queues, thread handoff and limits.
 * state_load has no instance argument, matching plugin_state_load.
 * The API table belongs to the library; keep it loaded until all instances die.
 * Metadata is exclusively in the external product.json.
 * Unsupported ABI versions return NULL.
 */
#ifdef __cplusplus
extern "C" {
#endif
const perone_api *perone_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
#endif
