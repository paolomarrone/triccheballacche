#ifndef CONTROLS_H
#define CONTROLS_H
#include "score_view.h"
#include "ui.h"
#include "webui.h"

typedef struct {
	int node; // -1 when detached; one view consumes each DSP message stream.
	UI *native;
} Controls;

void controls_close(Controls *controls, Session *session);
void controls_command(
    Controls *controls, Session *session, const ScoreView *view, unsigned revision, webui_event_t *event);
#endif
