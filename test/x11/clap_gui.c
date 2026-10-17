#include "test/clap/plugin.h"
bool gui_supported(const clap_plugin_t *plugin, const char *api, bool floating) {
	main_thread(self(plugin));
	return !floating && api && !strcmp(api, CLAP_WINDOW_API_X11);
}
bool gui_create(const clap_plugin_t *plugin, const char *api, bool floating) {
	Plugin *p = self(plugin);
	if (!gui_supported(plugin, api, floating) || p->display)
		return false;
	p->display = XOpenDisplay(NULL);
	if (!p->display)
		return false;
	p->window = XCreateSimpleWindow(p->display, DefaultRootWindow(p->display), 0, 0, 240, 90, 0, 0, 0x202020);
	XSelectInput(p->display, p->window, ExposureMask | ButtonPressMask);
	assert(p->host_timer->register_timer(p->host, 10, &p->timer));
	return true;
}
void gui_destroy(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(p->host_timer->unregister_timer(p->host, p->timer));
	XDestroyWindow(p->display, p->window);
	XCloseDisplay(p->display);
	p->display = NULL;
}
bool gui_size(const clap_plugin_t *plugin, uint32_t *w, uint32_t *h) {
	main_thread(self(plugin));
	*w = 240;
	*h = 90;
	return true;
}
bool gui_resizable(const clap_plugin_t *plugin) {
	main_thread(self(plugin));
	return false;
}
bool gui_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
	Plugin *p = self(plugin);
	main_thread(p);
	XReparentWindow(p->display, p->window, window->x11, 0, 0);
	return true;
}
bool gui_show(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	XMapWindow(p->display, p->window);
	XFlush(p->display);
	return true;
}
bool gui_hide(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	XUnmapWindow(p->display, p->window);
	return true;
}
void timer(const clap_plugin_t *plugin, clap_id id) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(id == p->timer);
	while (XPending(p->display)) {
		XEvent event;
		XNextEvent(p->display, &event);
		if (event.type == ButtonPress) {
			p->values[0] = .75;
			p->edited = 1;
			p->host_params->request_flush(p->host);
		}
	}
	char text[64];
	snprintf(text, sizeof(text), "CLAP gain: %.2f (click to change)", p->values[0]);
	GC gc = DefaultGC(p->display, DefaultScreen(p->display));
	XSetForeground(p->display, gc, 0xffffff);
	XClearWindow(p->display, p->window);
	XDrawString(p->display, p->window, gc, 10, 45, text, strlen(text));
	XFlush(p->display);
}
