#ifndef WEB_HOST_H
#define WEB_HOST_H
#include "daw.h"

typedef struct {
	Session session;
	Output output;
	ScoreView *view;
	float buffer[BLOCK * 2];
} Score;
#endif
