#ifndef HOST_LV2_H
#define HOST_LV2_H
#include "module.h"
#include "json_write.h"
#include <lilv/lilv.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/data-access/data-access.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/log/log.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/port-groups/port-groups.h>
#include <lv2/port-props/port-props.h>
#include <lv2/resize-port/resize-port.h>
#include <lv2/time/time.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/state/state.h>
#include <pthread.h>
#include <semaphore.h>

enum { LV2_PORT_LIMIT = 4096, LV2_ATOM_CAPACITY = 65536 };
enum { LV2_PORT_CONTROL = 1, LV2_PORT_AUDIO, LV2_PORT_ATOM };
typedef struct {
	int type, input, channel, parameter, midi, time, scaled, freewheel;
	float value;
	LV2_Atom_Sequence *events;
} Lv2Port;
typedef struct {
	LilvWorld *world; // Borrowed from the cached module.
	const LilvPlugin *plugin;
	Lv2Port *ports;
	uint32_t count;
	int inputs, outputs, main_inputs, main_outputs, midi, latency, nparams;
} Lv2Description;
typedef struct {
	const LV2_Worker_Interface *api;
	LV2_Worker_Schedule schedule;
	Messages requests, responses;
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t done;
	sem_t ready;
	atomic_int stop, pending;
	int started, synchronous;
} Lv2Worker;
typedef struct {
	DSP *dsp;
	Lv2Description description;
	LilvInstance *instance;
	LV2_URID_Map map;
	LV2_URID_Unmap unmap;
	pthread_mutex_t uris_mutex;
	char **uris;
	uint32_t nuri;
	LV2_URID sequence, chunk, midi, event_transfer;
	LV2_URID position, frame, speed, tempo, beat;
	LV2_Atom_Forge forge;
	LV2_Log_Log log;
	LV2_Feature features[8];
	const LV2_Feature *feature_ptrs[9];
	LV2_Options_Option options[6];
	int32_t min_block, max_block, sequence_size;
	float rate, scale;
	float *scratch;
	int active, dirty;
	atomic_int error;
	atomic_uint latency;
	uint32_t applied_latency;
	Messages from_ui, to_ui;
	Lv2Worker worker;
} Lv2;

LilvWorld *lv2_world_open(const char *path);
int lv2_describe(Lv2Description *d, LilvWorld *world, const char *id);
int lv2_metadata(Lv2Description *d, Json *json, const char *path);
int lv2_scan(Json *json, const char *path);
int lv2_required(LilvWorld *world, const LilvNode *subject, int ui);
LilvUIs *lv2_uis(Lv2Description *d, const LilvUI **selected);
int lv2_ui_fixed(Lv2Description *d, const LilvUI *ui);
LilvNode *lv2_get(LilvWorld *world, const LilvNode *subject, const char *predicate);
int lv2_is(LilvWorld *world, const LilvNode *subject, const char *predicate, const char *object);
int lv2_ui_send(Lv2 *l, uint32_t port, uint32_t size, const void *data);
LV2_Worker_Status lv2_schedule(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data);
int lv2_worker_start(Lv2 *l);
void lv2_worker_wait(Lv2 *l);
void lv2_worker_emit(Lv2 *l);
void lv2_worker_close(Lv2 *l);
#endif
