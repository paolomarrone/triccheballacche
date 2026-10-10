#include "ui_internal.h"
#include "clap.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	Clap *clap;
	Display *display;
	Window parent;
	Atom quit;
	uint32_t width, height;
	int created;
} ClapUI;

static bool resize(void *view, uint32_t width, uint32_t height) {
	ClapUI *ui = view;
	if (!width || !height || width > 16384 || height > 16384)
		return false;
	ui->width = width;
	ui->height = height;
	XResizeWindow(ui->display, ui->parent, width, height);
	XFlush(ui->display);
	return true;
}
static void show(void *view, int visible) {
	ClapUI *ui = view;
	if (visible) {
		XMapRaised(ui->display, ui->parent);
		ui->clap->gui->show(ui->clap->plugin);
	} else {
		ui->clap->gui->hide(ui->clap->plugin);
		XUnmapWindow(ui->display, ui->parent);
	}
	XFlush(ui->display);
}
static void close_view(void *view) {
	ClapUI *ui = view;
	if (!ui)
		return;
	Clap *c = ui->clap;
	c->window = NULL;
	c->resize = NULL;
	c->show = NULL;
	if (ui->created) {
		c->gui->hide(c->plugin);
		c->gui->destroy(c->plugin);
	}
	atomic_fetch_and(&c->requests, ~CLAP_HOST_CLOSED);
	if (ui->parent)
		XDestroyWindow(ui->display, ui->parent);
	if (ui->display)
		XCloseDisplay(ui->display);
	free(ui);
}
static int open_view(void **out, Session *session, int id) {
	*out = NULL;
	DSP *dsp = session->nodes[id].dsp[0].dsp;
	Clap *c = dsp->instance;
	const clap_plugin_gui_t *g = c->gui;
	if (!g || c->window || !g->is_api_supported(c->plugin, CLAP_WINDOW_API_X11, false))
		return -1;
	ClapUI *ui = calloc(1, sizeof(*ui));
	if (!ui)
		return -1;
	ui->clap = c;
	ui->display = XOpenDisplay(NULL);
	if (!ui->display)
		goto fail;
	if (!g->create(c->plugin, CLAP_WINDOW_API_X11, false))
		goto fail;
	ui->created = 1;
	if (!g->get_size(c->plugin, &ui->width, &ui->height) || !ui->width || !ui->height || ui->width > 16384 ||
	    ui->height > 16384)
		goto fail;
	ui->parent = XCreateSimpleWindow(ui->display, DefaultRootWindow(ui->display), 0, 0, ui->width, ui->height, 0, 0, 0);
	XStoreName(ui->display, ui->parent, session_score(session)->nodes[id].info.name);
	XSelectInput(ui->display, ui->parent, StructureNotifyMask);
	ui->quit = XInternAtom(ui->display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(ui->display, ui->parent, &ui->quit, 1);
	if (!g->can_resize(c->plugin)) {
		XSizeHints hints = {.flags = PMinSize | PMaxSize,
		    .min_width = ui->width,
		    .max_width = ui->width,
		    .min_height = ui->height,
		    .max_height = ui->height};
		XSetWMNormalHints(ui->display, ui->parent, &hints);
	}
	XSync(ui->display, False);
	clap_window_t window = {.api = CLAP_WINDOW_API_X11, .x11 = ui->parent};
	if (!g->set_parent(c->plugin, &window))
		goto fail;
	c->window = ui;
	c->resize = resize;
	c->show = show;
	*out = ui;
	return 0;
fail:
	close_view(ui);
	return -1;
}
static int poll_view(void *view) {
	ClapUI *ui = view;
	Clap *c = ui->clap;
	if (atomic_load(&c->requests) & CLAP_HOST_CLOSED)
		return 1;
	while (XPending(ui->display)) {
		XEvent e;
		XNextEvent(ui->display, &e);
		if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == ui->quit)
			return 1;
		if (e.type == ConfigureNotify &&
		    ((uint32_t)e.xconfigure.width != ui->width || (uint32_t)e.xconfigure.height != ui->height)) {
			uint32_t w = e.xconfigure.width, h = e.xconfigure.height;
			if (c->gui->can_resize(c->plugin) && c->gui->adjust_size(c->plugin, &w, &h) &&
			    c->gui->set_size(c->plugin, w, h))
				resize(ui, w, h);
			else
				resize(ui, ui->width, ui->height);
		}
	}
	return 0;
}
const UIOps clap_ui_ops = {open_view, show, poll_view, close_view};
