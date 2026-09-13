#include "host.h"
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

static Score *prepare(const char *path, const char *source, unsigned sample_rate, int project) {
	Score *score = calloc(1, sizeof(*score));
	if (!score)
		return NULL;
	score->session.sample_rate = sample_rate;
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
	return prepare(path, NULL, sample_rate, 0);
}

Score *score_prepare(const char *path, const char *source, unsigned sample_rate) {
	return prepare(path, source, sample_rate, 1);
}

size_t score_frames(Score *score) {
	return score->session.frames;
}

float score_normalize(Score *score) {
	return score->output.normalize;
}

float *score_buffer(Score *score) {
	return score->buffer;
}

int score_render(Score *score) {
	Session *s = &score->session;
	size_t n = s->frames - s->time;
	if (n > BLOCK)
		n = BLOCK;
	return session_render(s, score->buffer, n) ? -1 : (int)n;
}

DSP *score_dsp(Score *score, int node) {
	return node >= 0 && node < score->session.nnodes ? score->session.nodes[node].dsp[0].dsp : NULL;
}
