#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H
#include "ui.h"
typedef struct {
	int (*open)(void **view, Session *session, int node);
	void (*show)(void *view, int visible);
	int (*poll)(void *view);
	void (*close)(void *view);
} UIOps;
extern const UIOps perone_ui_ops, clap_ui_ops, lv2_ui_ops, vst3_ui_ops;
#endif
