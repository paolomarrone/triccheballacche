#include "test/platform.h"
#include "host/platform.h"
#include <X11/Xlib.h>
#include <assert.h>
#include <string.h>
static Window named_window(Display *display, Window parent, const char *name) {
	char *title = NULL;
	if (XFetchName(display, parent, &title) && title) {
		int matches = !strcmp(title, name);
		XFree(title);
		if (matches)
			return parent;
	}
	Window root, up, *children;
	unsigned count;
	if (!XQueryTree(display, parent, &root, &up, &children, &count))
		return 0;
	Window result = 0;
	for (unsigned i = 0; i < count && !result; ++i)
		result = named_window(display, children[i], name);
	XFree(children);
	return result;
}
void test_ui_wait(void) {
	host_sleep(10);
}
void test_clap_click(void) {
	Display *d = XOpenDisplay(NULL);
	assert(d);
	Window parent = named_window(d, DefaultRootWindow(d), "CLAP test synth");
	assert(parent);
	Window root, up, *children;
	unsigned count;
	assert(XQueryTree(d, parent, &root, &up, &children, &count) && count);
	XEvent event = {.xbutton = {.type = ButtonPress,
	                    .display = d,
	                    .window = children[0],
	                    .button = Button1,
	                    .same_screen = True,
	                    .x = 30,
	                    .y = 30}};
	XSendEvent(d, children[0], False, ButtonPressMask, &event);
	XFlush(d);
	XFree(children);
	XCloseDisplay(d);
}
