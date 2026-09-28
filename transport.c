#include "transport.h"
#include "score_view_json.h"
#include "json_write.h"
#include <stdlib.h>
#include <string.h>

void transport_attach(Transport *t, Session *session, ScoreView *view) {
	score_view_free(&t->view);
	score_view_free(&t->queued);
	t->session = session;
	t->view = *view;
	*view = (ScoreView){0};
	t->control_revision = ++t->revision;
	t->playing = 1;
	t->time = 0;
}

int transport_live(const Transport *t) {
	const Score *score = t->session ? session_score(t->session) : NULL;
	return score && score->live;
}

int transport_collect(Transport *t) {
	if (!t->session || !t->queued.nnodes)
		return 0;
	unsigned revision = atomic_load(&t->session->revision);
	if (revision <= t->revision)
		return 0;
	score_view_free(&t->view);
	t->view = t->queued;
	t->queued = (ScoreView){0};
	t->revision = revision;
	score_view_activate(&t->view, t->session->active->at);
	session_collect(t->session);
	return 1;
}

int transport_update(Transport *t, Score *score, ScoreView *view, uint64_t position) {
	if (t->queued.nnodes) {
		score->error = "Wait for the pending revision to become active";
		return -1;
	}
	if (!t->playing || !transport_live(t)) {
		score->error = "Live updates require a playing live score";
		return -1;
	}
	int mapping[MAX_NODES];
	// Leave 100 ms to publish before the selected musical boundary.
	if (session_update(t->session, score, position + t->session->sample_rate / 10, t->revision + 1, mapping))
		return -1;
	score_view_remap(view, mapping);
	t->queued = *view;
	*view = (ScoreView){0};
	return 0;
}

void transport_started(Transport *t) {
	t->playing = t->session != NULL;
}

void transport_stopped(Transport *t) {
	t->playing = 0;
	if (t->session) {
		transport_collect(t);
		session_cancel(t->session);
		t->time = (double)t->session->time / t->session->sample_rate;
	}
	score_view_free(&t->queued);
}

void transport_seeked(Transport *t) {
	t->time = (double)t->session->time / t->session->sample_rate;
	score_view_activate(&t->view, t->session->active->at);
}

void transport_detach(Transport *t) {
	t->session = NULL;
	t->playing = 0;
	score_view_free(&t->queued);
}

void transport_free(Transport *t) {
	score_view_free(&t->view);
	score_view_free(&t->queued);
	*t = (Transport){0};
}

char *transport_json(const Transport *t, const char *op, double position, const double args[6]) {
	double time = t->playing ? position : t->time;
	char *view = NULL;
	int query = !strcmp(op, "range") || !strcmp(op, "origin") || !strcmp(op, "automation");
	if (query)
		view = score_view_json(&t->view, t->revision, op, args[0], args[1], args[2], args[3], args[4], args[5]);
	else if (!strcmp(op, "score") || !strcmp(op, "status"))
		view = score_view_json(&t->view, t->revision, op, time, t->playing, 0, 0, 0, 0);
	if (!view && (query || !strcmp(op, "score") || !strcmp(op, "status")))
		return NULL;
	Json json = {0};
	json_print(&json, "{\"prepared\":%s,\"playing\":%s,\"time\":%.17g,\"revision\":%u,\"controlRevision\":%u,",
	    t->session ? "true" : "false", t->playing ? "true" : "false", time, t->revision, t->control_revision);
	json_print(&json, "\"queued\":%s,\"live\":%s,\"view\":%s}", t->queued.nnodes ? "true" : "false",
	    transport_live(t) ? "true" : "false", view ? view : "null");
	free(view);
	if (json.failed) {
		free(json.data);
		return NULL;
	}
	return json.data;
}
