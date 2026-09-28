#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H
#include "score_janet.h"

static inline int prepare_session(
    Session *s, Output *output, const char *path, const char *source, char **diagnostics, ScoreView *view) {
	Score score = {.sample_rate = s->sample_rate};
	int result = prepare_score(&score, output, path, source, diagnostics, view);
	if (result)
		s->error = score.error;
	else
		result = session_activate(s, &score);
	score_free(&score);
	return result;
}

static inline int start_score(Session *s, Score *score, uint64_t frames) {
	int result = score_end(score, frames);
	return result ? result : session_activate(s, score);
}
#endif
