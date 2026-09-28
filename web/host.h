#ifndef WEB_HOST_H
#define WEB_HOST_H
#include "score_janet.h"

typedef struct {
	Score description;
	Session session;
	Output output;
	ScoreView *view;
	float buffer[BLOCK * 2];
} WebScore;
#endif
