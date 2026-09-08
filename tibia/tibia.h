/* Tibia shared ABI v1. GPL-3.0-or-later. */
#ifndef TIBIA_SHARED_H
#define TIBIA_SHARED_H
#include <stddef.h>
#include <stdint.h>

#define TIBIA_ABI_VERSION 1
enum { TIBIA_PARAM_OUTPUT = 1, TIBIA_PARAM_INTEGER = 2, TIBIA_PARAM_TOGGLED = 4,
	TIBIA_PARAM_LOG = 8, TIBIA_PARAM_BYPASS = 16, TIBIA_PARAM_LATENCY = 32, TIBIA_PARAM_LIST = 64 };
enum { TIBIA_BUS_OUTPUT = 1, TIBIA_BUS_MIDI = 2, TIBIA_BUS_OPTIONAL = 4,
	TIBIA_BUS_CV = 8, TIBIA_BUS_SIDECHAIN = 16 };
typedef struct {
	const char *id, *name, *unit;
	float minimum, maximum, default_value;
	uint32_t flags;
} tibia_parameter;
typedef struct {
	const char *id, *name;
	uint32_t flags, channels; /* MIDI: channels = 0; audio: 1 or 2. */
} tibia_bus;
typedef struct {
	size_t bus_count;
	const tibia_bus *buses;
	size_t count;
	const tibia_parameter *parameters;
	const char *json; /* Complete {"product": ...} descriptor, UTF-8. */
} tibia_info;
typedef struct {
	void *handle;
	const char *(*get_bindir)(void *handle);
	const char *(*get_datadir)(void *handle);
} tibia_callbacks;
typedef struct {
	tibia_info info;
	void *(*create)(float sample_rate, const tibia_callbacks *callbacks);
	void (*destroy)(void *instance);
	void (*reset)(void *instance);
	void (*process)(void *instance, const float **inputs, float **outputs, size_t frames);
	void (*set_parameter)(void *instance, size_t index, float value); /* NULL without inputs. */
	float (*get_parameter)(void *instance, size_t index); /* NULL without outputs. */
	void (*midi_msg_in)(void *instance, size_t bus, const uint8_t *data); /* Optional. */
} tibia_api;

/* All pointers are owned by the library; keep it loaded until all instances die.
 * create sets defaults, sample rate, memory and resets; NULL means failure.
 * reset preserves parameters. Sample rate is fixed for the instance lifetime.
 * Parameter and MIDI bus indices are the original product.json array indices.
 * Audio channels are flattened in JSON bus order, separately for each direction.
 * Values use product units, not normalized 0..1. Only output parameters are read.
 * MIDI input points to three bytes (pad unused data bytes with zero).
 * Calls on an instance must be serialized. No allocation in process or setters.
 * Unsupported ABI versions return NULL; no legacy symbol fallback.
 */
#ifdef __cplusplus
extern "C" {
#endif
const tibia_api *tibia_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
#endif
