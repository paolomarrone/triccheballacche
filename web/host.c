#include "host.h"
#include "snapshot.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void view_free(ScoreView *view) {
	if (view) {
		score_view_free(view);
		free(view);
	}
}

ScoreView *score_take_view(WebScore *score) {
	ScoreView *view = score->view;
	score->view = NULL;
	return view;
}

void web_score_free(WebScore *score) {
	if (score) {
		session_free(&score->session);
		score_free(&score->description);
		view_free(score->view);
		free(score);
	}
}

static WebScore *prepare(const char *path, const char *source, unsigned sample_rate, int project, int describe) {
	WebScore *score = calloc(1, sizeof(*score));
	if (!score)
		return NULL;
	score->description.sample_rate = sample_rate;
	if (project && !(score->view = calloc(1, sizeof(ScoreView)))) {
		web_score_free(score);
		return NULL;
	}
	if (prepare_score(&score->description, &score->output, path, source, NULL, score->view) ||
	    (!describe && session_activate(&score->session, &score->description))) {
		fprintf(stderr, "Score failed: %s\n",
		    score->description.error   ? score->description.error
		        : score->session.error ? score->session.error
		                               : "Janet error");
		web_score_free(score);
		return NULL;
	}
	return score;
}

WebScore *score_new(const char *path, unsigned sample_rate) {
	return prepare(path, NULL, sample_rate, 0, 0);
}

WebScore *score_prepare(const char *path, const char *source, unsigned sample_rate) {
	return prepare(path, source, sample_rate, 1, 0);
}

size_t score_frames(WebScore *score) {
	return score->session.frames == UINT64_MAX ? 0 : score->session.frames;
}

double score_duration(WebScore *score) {
	Session *s = &score->session;
	return s->frames == UINT64_MAX ? INFINITY : (double)s->frames / s->sample_rate;
}

int score_can_seek(WebScore *score, double seconds) {
	return session_frame(&score->session, seconds) != UINT64_MAX;
}

float score_normalize(WebScore *score) {
	return score->output.normalize;
}

float *score_buffer(WebScore *score) {
	return score->buffer;
}

int score_render(WebScore *score) {
	Session *s = &score->session;
	size_t n = s->frames - s->time < BLOCK ? (size_t)(s->frames - s->time) : BLOCK;
	return session_render(s, score->buffer, n) ? -1 : (int)n;
}

DSP *score_dsp(WebScore *score, int node) {
	return node >= 0 && node < score->session.nnodes ? score->session.nodes[node].dsp[0].dsp : NULL;
}

int score_listen(WebScore *score, int track, int flags) {
	return session_listen(&score->session, track, flags);
}

WebScore *score_describe(const char *path, const char *source, unsigned sample_rate) {
	return prepare(path, source, sample_rate, 1, 1);
}

static size_t packed_length;
void *score_pack_web(WebScore *s) {
	return score_pack(&s->description, &s->output, s->view, &packed_length);
}
size_t score_pack_length(void) {
	return packed_length;
}

WebScore *score_import(const void *data, size_t length) {
	WebScore *s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->view = calloc(1, sizeof(ScoreView));
	if (!s->view || score_unpack(&s->description, &s->output, s->view, data, length)) {
		web_score_free(s);
		return NULL;
	}
	return s;
}
int web_score_activate(WebScore *s) {
	return session_activate(&s->session, &s->description);
}
int score_live(WebScore *s) {
	const Score *description = session_score(&s->session);
	return description ? description->live : s->description.live;
}
unsigned score_revision(WebScore *s) {
	return atomic_load(&s->session.revision);
}
void score_cancel(WebScore *s) {
	session_cancel(&s->session);
}

void score_view_activate_web(WebScore *score, ScoreView *view) {
	score_view_activate(view, score->session.active->at);
	session_collect(&score->session);
}
