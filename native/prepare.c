#include "prepare.h"
#include "script.h"
#include "native/thread.h"

typedef struct {
	Mutex mutex;
	Condition ready;
	Score *score;
	Output *output;
	const char *path, *source;
	char **diagnostics;
	ScoreView *view;
	JanetVM *vm;
	int done, result;
} Preparation;

static void *evaluate(void *context) {
	Preparation *p = context;
	mutex_lock(&p->mutex);
	p->vm = janet_local_vm();
	condition_signal(&p->ready);
	mutex_unlock(&p->mutex);
	int result = prepare_score(p->score, p->output, p->path, p->source, p->diagnostics, p->view);
	mutex_lock(&p->mutex);
	p->result = result;
	p->done = 1;
	condition_signal(&p->ready);
	mutex_unlock(&p->mutex);
	return NULL;
}

int prepare_background(
    Score *score, Output *output, const char *path, const char *source, char **diagnostics, ScoreView *view) {
	Preparation p = {.mutex = MUTEX_INITIALIZER,
	    .ready = CONDITION_INITIALIZER,
	    .score = score,
	    .output = output,
	    .path = path,
	    .source = source,
	    .diagnostics = diagnostics,
	    .view = view};
	Thread thread;
	if (thread_start(&thread, evaluate, &p)) {
		score->error = "Cannot start preparation worker";
		condition_destroy(&p.ready);
		mutex_destroy(&p.mutex);
		return -1;
	}
	mutex_lock(&p.mutex);
	while (!p.done) {
		if (condition_wait_ms(&p.ready, &p.mutex, 5000) == 1 && p.vm && !p.done)
			janet_interpreter_interrupt(p.vm);
	}
	mutex_unlock(&p.mutex);
	thread_join(thread);
	condition_destroy(&p.ready);
	mutex_destroy(&p.mutex);
	return p.result;
}
