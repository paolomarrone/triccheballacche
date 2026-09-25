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

ScoreView *score_take_view(Score *score) {
	ScoreView *view = score->view;
	score->view = NULL;
	return view;
}

void score_free(Score *score) {
	if (score) {
		session_free(&score->session);
		view_free(score->view);
		free(score);
	}
}

static Score *prepare(const char *path, const char *source, unsigned sample_rate, int project, int describe) {
	Score *score = calloc(1, sizeof(*score));
	if (!score)
		return NULL;
	score->session.sample_rate = sample_rate;
	score->session.describe = describe;
	if (project && !(score->view = calloc(1, sizeof(ScoreView)))) {
		score_free(score);
		return NULL;
	}
	if (prepare_score(&score->session, &score->output, path, source, NULL, score->view)) {
		fprintf(stderr, "Score failed: %s\n", score->session.error ? score->session.error : "Janet error");
		score_free(score);
		return NULL;
	}
	return score;
}

Score *score_new(const char *path, unsigned sample_rate) {
	return prepare(path, NULL, sample_rate, 0, 0);
}

Score *score_prepare(const char *path, const char *source, unsigned sample_rate) {
	return prepare(path, source, sample_rate, 1, 0);
}

size_t score_frames(Score *score) {
	return score->session.frames == UINT64_MAX ? 0 : score->session.frames;
}

double score_duration(Score *score) {
	Session *s = &score->session;
	return s->frames == UINT64_MAX ? INFINITY : (double)s->frames / s->sample_rate;
}

int score_can_seek(Score *score, double seconds) {
	return session_frame(&score->session, seconds) != UINT64_MAX;
}

float score_normalize(Score *score) {
	return score->output.normalize;
}

float *score_buffer(Score *score) {
	return score->buffer;
}

int score_render(Score *score) {
	Session *s = &score->session;
	size_t n = s->frames - s->time < BLOCK ? (size_t)(s->frames - s->time) : BLOCK;
	return session_render(s, score->buffer, n) ? -1 : (int)n;
}

DSP *score_dsp(Score *score, int node) {
	return node >= 0 && node < score->session.nnodes ? score->session.nodes[node].dsp[0].dsp : NULL;
}

int score_listen(Score *score, int track, int flags) {
	return session_listen(&score->session, track, flags);
}

Score *score_describe(const char *path, const char *source, unsigned sample_rate) {
	return prepare(path, source, sample_rate, 1, 1);
}

static size_t packed_length;
void *score_pack_web(Score *s) {
	return score_pack(&s->session, &s->output, s->view, &packed_length);
}
size_t score_pack_length(void) {
	return packed_length;
}

Score *score_import(const void *data, size_t length) {
	Score *s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->view = calloc(1, sizeof(ScoreView));
	if (!s->view || score_unpack(&s->session, &s->output, s->view, data, length)) {
		score_free(s);
		return NULL;
	}
	return s;
}
int score_activate(Score *s) {
	return session_activate(&s->session);
}
int score_live(Score *s) {
	return s->session.sequence != NULL;
}
unsigned score_revision(Score *s) {
	return atomic_load(&s->session.revision);
}
void score_cancel(Score *s) {
	session_cancel(&s->session);
}

void score_view_activate_web(Score *score, ScoreView *view) {
	score_view_activate(view, &score->session);
	session_collect(&score->session);
}
