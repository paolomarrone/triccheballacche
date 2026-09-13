#ifndef UI_H
#define UI_H
#include "session.h"

typedef struct UI UI;

// Create hidden on the UI thread. A bundle without a native UI succeeds with *ui == NULL.
int ui_open(UI **ui, Node *node);
// Show/hide on the UI thread; hiding keeps the instance and its control exchange alive.
void ui_show(UI *ui, int visible);
// UI thread only: 0 running, 1 window closed, -1 error.
int ui_poll(UI *ui);
// The borrowed node and DSP must outlive the UI.
void ui_close(UI *ui);
#endif
