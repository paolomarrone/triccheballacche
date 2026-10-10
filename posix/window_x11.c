#include "window_x11.h"
#include <X11/Xutil.h>

int window_size(PluginWindow *w, unsigned width, unsigned height) {
	if (!width || !height || width > 16384 || height > 16384)
		return -1;
	w->width = width;
	w->height = height;
	if (w->fixed) {
		XSizeHints hints = {.flags = PMinSize | PMaxSize,
		    .min_width = width,
		    .max_width = width,
		    .min_height = height,
		    .max_height = height};
		XSetWMNormalHints(w->display, w->parent, &hints);
	}
	XResizeWindow(w->display, w->parent, width, height);
	XFlush(w->display);
	return 0;
}
int window_open(PluginWindow *w, const char *title, unsigned width, unsigned height, int fixed) {
	if (!width || !height || width > 16384 || height > 16384)
		return -1;
	w->display = XOpenDisplay(NULL);
	if (!w->display)
		return -1;
	w->fixed = fixed;
	w->parent = XCreateSimpleWindow(w->display, DefaultRootWindow(w->display), 0, 0, width, height, 0, 0, 0);
	XStoreName(w->display, w->parent, title);
	XSelectInput(w->display, w->parent, StructureNotifyMask);
	w->quit = XInternAtom(w->display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(w->display, w->parent, &w->quit, 1);
	window_size(w, width, height);
	XSync(w->display, False);
	return 0;
}
void window_close(PluginWindow *w) {
	if (w->parent)
		XDestroyWindow(w->display, w->parent);
	if (w->display)
		XCloseDisplay(w->display);
	*w = (PluginWindow){0};
}
void window_show(PluginWindow *w, int visible) {
	if (visible)
		XMapRaised(w->display, w->parent);
	else
		XUnmapWindow(w->display, w->parent);
	XFlush(w->display);
}
int window_poll(PluginWindow *w, int (*resize)(void *, unsigned *, unsigned *), void *context) {
	unsigned width = w->width, height = w->height;
	while (XPending(w->display)) {
		XEvent event;
		XNextEvent(w->display, &event);
		if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == w->quit)
			return 1;
		if (event.type == ConfigureNotify && event.xconfigure.window == w->parent) {
			width = event.xconfigure.width;
			height = event.xconfigure.height;
		}
	}
	if (width != w->width || height != w->height) {
		// Apply the latest geometry once; replaying old notifications causes resize loops.
		unsigned accepted_width = width, accepted_height = height;
		if (w->fixed || !resize || resize(context, &accepted_width, &accepted_height))
			window_size(w, w->width, w->height);
		else if (accepted_width != width || accepted_height != height)
			window_size(w, accepted_width, accepted_height);
		else {
			w->width = width;
			w->height = height;
		}
	}
	return 0;
}
