#ifndef HOST_WINDOW_H
#define HOST_WINDOW_H
// Native handles and window-system headers stay in the backend implementation.
typedef struct {
	void *platform;
	unsigned width, height;
	int fixed;
} PluginWindow;
struct clap_window;
int window_open(PluginWindow *window, const char *title, unsigned width, unsigned height, int fixed);
void window_close(PluginWindow *window);
void window_show(PluginWindow *window, int visible);
int window_size(PluginWindow *window, unsigned width, unsigned height);
void *window_parent(PluginWindow *window);
void window_clap_parent(PluginWindow *window, struct clap_window *parent);
int window_child_size(PluginWindow *window, void *child, unsigned *width, unsigned *height);
// Resize returns zero to accept (and may adjust dimensions), nonzero to keep the old size.
int window_poll(PluginWindow *window, int (*resize)(void *, unsigned *, unsigned *), void *context);
#endif
