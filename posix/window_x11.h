#ifndef WINDOW_X11_H
#define WINDOW_X11_H
#include <X11/Xlib.h>

typedef struct {
	Display *display;
	Window parent;
	Atom quit;
	unsigned width, height;
	int fixed;
} PluginWindow;
int window_open(PluginWindow *window, const char *title, unsigned width, unsigned height, int fixed);
void window_close(PluginWindow *window);
void window_show(PluginWindow *window, int visible);
int window_size(PluginWindow *window, unsigned width, unsigned height);
// Resize returns zero to accept (and may adjust dimensions), nonzero to keep the old size.
int window_poll(PluginWindow *window, int (*resize)(void *, unsigned *, unsigned *), void *context);
#endif
