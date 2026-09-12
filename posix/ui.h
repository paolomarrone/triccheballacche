#ifndef UI_H
#define UI_H
#include "session.h"

typedef struct UI UI;

// Open before playback. A bundle without a native UI succeeds with *ui == NULL.
int ui_open(UI **ui, Node *node);
// UI thread only: 0 running, 1 window closed, -1 error.
int ui_poll(UI *ui);
// Stop the player first; the borrowed node and DSP must outlive the UI.
void ui_close(UI *ui);
#endif
