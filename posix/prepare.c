#include "prepare.h"
#include "script.h"
#include <errno.h>
#include <pthread.h>
#include <time.h>

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t ready;
	Session *session;
	Output *output;
	const char *path, *source;
	char **diagnostics;
	ScoreView *view;
	JanetVM *vm;
	int done, result;
} Preparation;

static void *evaluate(void *context) {
	Preparation *p = context;
	pthread_mutex_lock(&p->mutex);
	p->vm = janet_local_vm();
	pthread_cond_signal(&p->ready);
	pthread_mutex_unlock(&p->mutex);
	int result = prepare_score(p->session, p->output, p->path, p->source, p->diagnostics, p->view);
	pthread_mutex_lock(&p->mutex);
	p->result = result;
	p->done = 1;
	pthread_cond_signal(&p->ready);
	pthread_mutex_unlock(&p->mutex);
	return NULL;
}

int prepare_background(
    Session *session, Output *output, const char *path, const char *source, char **diagnostics, ScoreView *view) {
	Preparation p = {.mutex = PTHREAD_MUTEX_INITIALIZER,
	    .ready = PTHREAD_COND_INITIALIZER,
	    .session = session,
	    .output = output,
	    .path = path,
	    .source = source,
	    .diagnostics = diagnostics,
	    .view = view};
	pthread_t thread;
	if (pthread_create(&thread, NULL, evaluate, &p)) {
		session->error = "Cannot start preparation worker";
		pthread_cond_destroy(&p.ready);
		pthread_mutex_destroy(&p.mutex);
		return -1;
	}
	pthread_mutex_lock(&p.mutex);
	while (!p.done) {
		struct timespec deadline;
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += 5;
		if (pthread_cond_timedwait(&p.ready, &p.mutex, &deadline) == ETIMEDOUT && p.vm && !p.done)
			janet_interpreter_interrupt(p.vm);
	}
	pthread_mutex_unlock(&p.mutex);
	pthread_join(thread, NULL);
	pthread_cond_destroy(&p.ready);
	pthread_mutex_destroy(&p.mutex);
	return p.result;
}
