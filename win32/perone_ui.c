#include "host/ui_internal.h"

// Perone UI ABI v1 currently specifies X11 and web, with no HWND API.
int perone_ui_available(const Node *node) {
	(void)node;
	return 0;
}
static int open_view(void **out, Session *session, int node) {
	(void)session;
	(void)node;
	*out = NULL;
	return -1;
}
const UIOps perone_ui_ops = {.open = open_view};
