#include "host/window.h"
#include <clap/clap.h>
#include <stdint.h>
#include <X11/Xutil.h>
#include <stdlib.h>

typedef struct {
	Display *display;
	Window parent;
	Atom quit;
} NativeWindow;
static NativeWindow *native(PluginWindow *w) {
	return w->platform;
}

void *window_parent(PluginWindow *w) {
	return (void *)(uintptr_t)native(w)->parent;
}
void window_clap_parent(PluginWindow *w, struct clap_window *parent) {
	*parent = (clap_window_t){.api = CLAP_WINDOW_API_X11, .x11 = native(w)->parent};
}
int window_child_size(PluginWindow *w, void *child, unsigned *width, unsigned *height) {
	// A UI may use a separate X connection. Wait until its child reaches the server.
	Window root, parent, *children = NULL;
	unsigned count = 0;
	int result = -1;
	if (XQueryTree(native(w)->display, native(w)->parent, &root, &parent, &children, &count)) {
		for (unsigned i = 0; i < count; ++i)
			if (children[i] == (Window)(uintptr_t)child) {
				XWindowAttributes attr;
				if (XGetWindowAttributes(native(w)->display, children[i], &attr)) {
					*width = attr.width;
					*height = attr.height;
					result = 0;
				}
			}
		XFree(children);
	}
	return result;
}

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
		XSetWMNormalHints(native(w)->display, native(w)->parent, &hints);
	}
	XResizeWindow(native(w)->display, native(w)->parent, width, height);
	XFlush(native(w)->display);
	return 0;
}
int window_open(PluginWindow *w, const char *title, unsigned width, unsigned height, int fixed) {
	w->platform = calloc(1, sizeof(NativeWindow));
	if (!w->platform)
		return -1;
	if (!width || !height || width > 16384 || height > 16384)
		return -1;
	native(w)->display = XOpenDisplay(NULL);
	if (!native(w)->display)
		return -1;
	w->fixed = fixed;
	native(w)->parent =
	    XCreateSimpleWindow(native(w)->display, DefaultRootWindow(native(w)->display), 0, 0, width, height, 0, 0, 0);
	XStoreName(native(w)->display, native(w)->parent, title);
	XSelectInput(native(w)->display, native(w)->parent, StructureNotifyMask);
	native(w)->quit = XInternAtom(native(w)->display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(native(w)->display, native(w)->parent, &native(w)->quit, 1);
	window_size(w, width, height);
	XSync(native(w)->display, False);
	return 0;
}
void window_close(PluginWindow *w) {
	if (!w->platform)
		return;
	if (native(w)->parent)
		XDestroyWindow(native(w)->display, native(w)->parent);
	if (native(w)->display)
		XCloseDisplay(native(w)->display);
	free(w->platform);
	*w = (PluginWindow){0};
}
void window_show(PluginWindow *w, int visible) {
	if (visible)
		XMapRaised(native(w)->display, native(w)->parent);
	else
		XUnmapWindow(native(w)->display, native(w)->parent);
	XFlush(native(w)->display);
}
int window_poll(PluginWindow *w, int (*resize)(void *, unsigned *, unsigned *), void *context) {
	unsigned width = w->width, height = w->height;
	while (XPending(native(w)->display)) {
		XEvent event;
		XNextEvent(native(w)->display, &event);
		if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == native(w)->quit)
			return 1;
		if (event.type == ConfigureNotify && event.xconfigure.window == native(w)->parent) {
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
