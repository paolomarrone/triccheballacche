#include "host/ui_internal.h"
#include "host/clap.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	Clap *clap;
	PluginWindow window;
	int created;
} ClapUI;

static bool resize(void *view, uint32_t width, uint32_t height) {
	ClapUI *ui = view;
	return !window_size(&ui->window, width, height);
}
static void show(void *view, int visible) {
	ClapUI *ui = view;
	if (visible) {
		window_show(&ui->window, 1);
		ui->clap->gui->show(ui->clap->plugin);
	} else {
		ui->clap->gui->hide(ui->clap->plugin);
		window_show(&ui->window, 0);
	}
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
	window_close(&ui->window);
	free(ui);
}
static int open_view(void **out, Session *session, int id) {
	*out = NULL;
	DSP *dsp = session->nodes[id].dsp[0].dsp;
	Clap *c = dsp->instance;
	const clap_plugin_gui_t *g = c->gui;
	if (!g || c->window || !g->is_api_supported(c->plugin, clap_window_api, false))
		return -1;
	ClapUI *ui = calloc(1, sizeof(*ui));
	if (!ui)
		return -1;
	ui->clap = c;
	if (!g->create(c->plugin, clap_window_api, false))
		goto fail;
	ui->created = 1;
	uint32_t width, height;
	if (!g->get_size(c->plugin, &width, &height) ||
	    window_open(&ui->window, session_score(session)->nodes[id].info.name, width, height, !g->can_resize(c->plugin)))
		goto fail;
	clap_window_t window;
	window_clap_parent(&ui->window, &window);
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
static int resized(void *view, unsigned *width, unsigned *height) {
	Clap *c = ((ClapUI *)view)->clap;
	return !c->gui->adjust_size(c->plugin, width, height) || !c->gui->set_size(c->plugin, *width, *height);
}
static int poll_view(void *view) {
	ClapUI *ui = view;
	if (atomic_load(&ui->clap->requests) & CLAP_HOST_CLOSED)
		return 1;
	return window_poll(&ui->window, resized, ui);
}
const UIOps clap_ui_ops = {open_view, show, poll_view, close_view};
