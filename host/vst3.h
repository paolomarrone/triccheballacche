#ifndef HOST_VST3_H
#define HOST_VST3_H
#include "dsp_internal.h"
#include "json_write.h"
#include "vst3_api.h"
#include <string.h>

// The ABI's first three methods are shared by every interface, including secondary ones.
static inline Steinberg_FUnknownVtbl vst_unknown(void *object) {
	void *table;
	Steinberg_FUnknownVtbl methods;
	memcpy(&table, object, sizeof(table));
	memcpy(&methods, table, sizeof(methods));
	return methods;
}
static inline void *vst_query(void *object, const Steinberg_TUID id) {
	void *result = NULL;
	if (object)
		vst_unknown(object).queryInterface(object, id, &result);
	return result;
}
static inline void vst_retain(void *object) {
	if (object)
		vst_unknown(object).addRef(object);
}
static inline void vst_release(void *object) {
	if (object)
		vst_unknown(object).release(object);
}
Steinberg_uint32 SMTG_STDMETHODCALLTYPE vst_borrow(void *object);
Steinberg_tresult vst_match(void *object, const Steinberg_TUID iid, const Steinberg_TUID own, void **out);

typedef struct VstHost VstHost;
VstHost *vst_host_new(void);
Steinberg_FUnknown *vst_host_context(VstHost *host);
void vst_host_poll(VstHost *host);
// The platform owns the native module entry/exit convention and its event loop.
void *vst_library_open(const char *path);
void vst_library_close(void *library);
void *vst_loop_new(void);
void vst_loop_poll(void *loop);

typedef struct {
	Steinberg_IBStream api;
	unsigned refs;
	unsigned char *data;
	size_t length, position, capacity;
	int failed;
} VstStream;
enum { VST_STATE_LIMIT = 64 * 1024 * 1024 };
VstStream *vst_stream_new(void);

typedef struct {
	double value;
	int offset, next;
} VstPoint;
typedef struct {
	Steinberg_Vst_IParamValueQueue api;
	Steinberg_Vst_ParamID id;
	struct VstChanges *changes;
	int head, tail, count, slot;
} VstQueue;
typedef struct VstChanges {
	Steinberg_Vst_IParameterChanges api;
	VstQueue *queues;
	VstPoint *points;
	int *order;
	int count, used, capacity;
	struct Vst *owner;
} VstChanges;
typedef struct {
	Steinberg_Vst_IEventList api;
	struct Steinberg_Vst_Event events[256];
	int count;
} VstEvents;
typedef struct {
	uint32_t id;
	int index;
} VstParameter;
typedef struct Vst {
	DSP *dsp;
	Module *module;
	Steinberg_Vst_IComponent *component;
	Steinberg_Vst_IAudioProcessor *processor;
	Steinberg_Vst_IEditController *controller;
	Steinberg_Vst_IConnectionPoint *connections[2];
	Steinberg_Vst_IComponentHandler handler;
	int initialized, controller_initialized, handler_set, connected[2], active, processing;
	atomic_uint requests;
	struct Steinberg_Vst_ParameterInfo *info;
	VstParameter *by_id;
	double *controller_values;
	int nparams, midi_map[16][130];
	VstChanges in, out;
	VstEvents events;
	struct Steinberg_Vst_AudioBusBuffers *inputs, *outputs;
	float **input_channels, **output_channels, *silence, *scratch;
	int ninputs, noutputs, input_slots, output_slots, midi;
	unsigned rate, block;
	Steinberg_TUID cid;
} Vst;

int vst_create(Vst *v, Module *module, const char *id);
void vst_destroy(Vst *v);
void vst_update_controller(Vst *v);
int vst_parameters(Vst *v);
int vst_parameter(Vst *v, uint32_t id);
int vst_layout(Vst *v, int *input, int *output, int *midi);
double vst_scale(const struct Steinberg_Vst_ParameterInfo *p);
void vst_string(char *out, size_t capacity, const Steinberg_char16 *in, size_t length);
void vst_id(char out[33], const Steinberg_TUID id);
int vst_scan(Json *json, const char *path);
Steinberg_IPluginFactory *vst_factory(Module *module);
VstHost *vst_host(Module *module);
extern const char *const vst_window_type;
#endif
