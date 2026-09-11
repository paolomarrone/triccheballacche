#ifndef WEB_HOST_H
#define WEB_HOST_H
#include "daw.h"

typedef struct {
	Session session;
	Output output;
	float buffer[BLOCK * 2];
} Score;
#endif
