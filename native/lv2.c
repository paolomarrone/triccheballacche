#include "host/lv2.h"
#include "util.h"
#include "native/thread.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
	const LV2_Worker_Interface *api;
	Messages requests, responses;
	Thread thread;
	Mutex mutex;
	Condition done;
	Semaphore ready;
	atomic_int stop, pending;
	int started, synchronous;
} Lv2Worker;
struct Lv2Runtime {
	Lv2Worker worker;
	Mutex uris_mutex;
	char **uris;
	uint32_t nuri;
};

int lv2_runtime_open(Lv2 *l) {
	Lv2Runtime *r = calloc(1, sizeof(*r));
	if (!r)
		return -1;
	if (mutex_init(&r->uris_mutex)) {
		free(r);
		return -1;
	}
	l->runtime = r;
	return 0;
}
void lv2_runtime_close(Lv2 *l) {
	Lv2Runtime *r = l->runtime;
	for (uint32_t i = 0; i < r->nuri; ++i)
		free(r->uris[i]);
	free(r->uris);
	mutex_destroy(&r->uris_mutex);
	free(r);
}
LV2_URID lv2_map_uri(void *handle, const char *uri) {
	Lv2Runtime *l = ((Lv2 *)handle)->runtime;
	mutex_lock(&l->uris_mutex);
	uint32_t i = 0;
	for (; i < l->nuri && strcmp(l->uris[i], uri); ++i) {
	}
	if (i == l->nuri) {
		char *copy = copy_string(uri);
		char **next = copy ? realloc(l->uris, (i + 1) * sizeof(*next)) : NULL;
		if (!next) {
			free(copy);
			mutex_unlock(&l->uris_mutex);
			return 0;
		}
		l->uris = next;
		l->uris[l->nuri++] = copy;
	}
	mutex_unlock(&l->uris_mutex);
	return i + 1;
}
const char *lv2_unmap_uri(void *handle, LV2_URID id) {
	Lv2Runtime *l = ((Lv2 *)handle)->runtime;
	mutex_lock(&l->uris_mutex);
	const char *uri = id && id <= l->nuri ? l->uris[id - 1] : NULL;
	mutex_unlock(&l->uris_mutex);
	return uri;
}
static LV2_Worker_Status respond(void *handle, uint32_t size, const void *data) {
	Lv2 *l = handle;
	return message_push(&l->runtime->worker.responses, size, data) ? LV2_WORKER_ERR_NO_SPACE : LV2_WORKER_SUCCESS;
}
static void work(Lv2 *l, uint32_t size, const void *data) {
	if (l->runtime->worker.api->work(lilv_instance_get_handle(l->instance), respond, l, size, data))
		atomic_store(&l->error, 1);
}
LV2_Worker_Status lv2_schedule(void *handle, uint32_t size, const void *data) {
	Lv2 *l = handle;
	Lv2Worker *w = &l->runtime->worker;
	if (!w->api || size > MAX_MESSAGE)
		return LV2_WORKER_ERR_NO_SPACE;
	if (w->synchronous) {
		work(l, size, data);
		return LV2_WORKER_SUCCESS;
	}
	atomic_fetch_add(&w->pending, 1);
	if (message_push(&w->requests, size, data)) {
		atomic_fetch_sub(&w->pending, 1);
		return LV2_WORKER_ERR_NO_SPACE;
	}
	semaphore_post(&w->ready);
	return LV2_WORKER_SUCCESS;
}
static void *worker_thread(void *handle) {
	Lv2 *l = handle;
	Lv2Worker *w = &l->runtime->worker;
	for (;;) {
		semaphore_wait(&w->ready);
		if (atomic_load(&w->stop))
			return NULL;
		uint64_t data[MAX_MESSAGE / sizeof(uint64_t)];
		size_t size;
		while (message_pop(&w->requests, &size, data)) {
			work(l, size, data);
			mutex_lock(&w->mutex);
			atomic_fetch_sub(&w->pending, 1);
			condition_signal(&w->done);
			mutex_unlock(&w->mutex);
		}
	}
}
int lv2_worker_start(Lv2 *l) {
	Lv2Worker *w = &l->runtime->worker;
	w->api = lilv_instance_get_extension_data(l->instance, LV2_WORKER__interface);
	w->synchronous = 1;
	if (!w->api)
		return 0;
	if (!w->api->work || !w->api->work_response)
		return -1;
	atomic_init(&w->stop, 0);
	atomic_init(&w->pending, 0);
	for (int i = 0; i < 2; ++i) {
		Messages *q = i ? &w->responses : &w->requests;
		q->limit = MAX_MESSAGE;
		q->data = malloc(MESSAGE_SLOTS * q->limit);
		atomic_init(&q->read, 0);
		atomic_init(&q->write, 0);
		if (!q->data)
			return -1;
	}
	if (semaphore_init(&w->ready))
		return -1;
	if (mutex_init(&w->mutex)) {
		semaphore_destroy(&w->ready);
		return -1;
	}
	if (condition_init(&w->done)) {
		mutex_destroy(&w->mutex);
		semaphore_destroy(&w->ready);
		return -1;
	}
	if (thread_start(&w->thread, worker_thread, l)) {
		condition_destroy(&w->done);
		mutex_destroy(&w->mutex);
		semaphore_destroy(&w->ready);
		return -1;
	}
	w->started = 1;
	return 0;
}
static void emit(Lv2 *l) {
	Lv2Worker *w = &l->runtime->worker;
	uint64_t data[MAX_MESSAGE / sizeof(uint64_t)];
	size_t size;
	for (int i = 0; w->started && i < MESSAGE_SLOTS && message_pop(&w->responses, &size, data); ++i)
		if (w->api->work_response(lilv_instance_get_handle(l->instance), size, data))
			atomic_store(&l->error, 1);
}
void lv2_worker_wait(Lv2 *l) {
	Lv2Worker *w = &l->runtime->worker;
	if (!w->started)
		return;
	mutex_lock(&w->mutex);
	while (atomic_load(&w->pending))
		condition_wait(&w->done, &w->mutex);
	mutex_unlock(&w->mutex);
	// Responses may schedule more work. Drain that work here, with no worker in flight.
	int synchronous = w->synchronous;
	w->synchronous = 1;
	while (lv2_worker_pending(l))
		emit(l);
	w->synchronous = synchronous;
}
void lv2_worker_close(Lv2 *l) {
	Lv2Worker *w = &l->runtime->worker;
	if (w->started) {
		lv2_worker_wait(l);
		atomic_store(&w->stop, 1);
		semaphore_post(&w->ready);
		thread_join(w->thread);
		condition_destroy(&w->done);
		mutex_destroy(&w->mutex);
		semaphore_destroy(&w->ready);
	}
	free(w->requests.data);
	free(w->responses.data);
}
void lv2_worker_begin(Lv2 *l, int offline) {
	Lv2Worker *w = &l->runtime->worker;
	if (offline)
		lv2_worker_wait(l);
	w->synchronous = offline;
	emit(l);
}
void lv2_worker_end(Lv2 *l) {
	Lv2Worker *w = &l->runtime->worker;
	emit(l);
	if (w->api && w->api->end_run)
		w->api->end_run(lilv_instance_get_handle(l->instance));
}
int lv2_worker_pending(Lv2 *l) {
	Lv2Worker *w = &l->runtime->worker;
	return w->started && atomic_load(&w->responses.read) != atomic_load(&w->responses.write);
}
