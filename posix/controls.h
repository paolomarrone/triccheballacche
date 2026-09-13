#ifndef CONTROLS_H
#define CONTROLS_H
#include "score_view.h"
#include "ui.h"
#include "webui.h"

typedef struct {
	UI *native[MAX_NODES];
	unsigned char watched[MAX_NODES]; // One web or native consumer per node.
} Controls;

void controls_close(Controls *controls, Session *session);
int controls_poll(Controls *controls, Session *session);
void controls_command(
    Controls *controls, Session *session, const ScoreView *view, unsigned revision, webui_event_t *event);
#endif
