#include "host.h"
#include <stdio.h>
#include <stdlib.h>

void score_free(Score *score) {
	if (score) {
		session_free(&score->session);
		free(score);
	}
}

Score *score_new(const char *path, unsigned sample_rate) {
	Score *score = calloc(1, sizeof(*score));
	if (!score)
		return NULL;
	score->session.sample_rate = sample_rate;
	if (load_score(&score->session, &score->output, path)) {
		fprintf(stderr, "Score failed: %s\n", score->session.error ? score->session.error : "Janet error");
		score_free(score);
		return NULL;
	}
	return score;
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
