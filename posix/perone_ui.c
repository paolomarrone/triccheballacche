#include "ui_internal.h"
#include "module.h"
#include "perone_ui.h"
#include "util.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
	Session *session;
	int node;
	Display *display;
	Window parent, widget;
	Atom quit;
	void *library, *instance;
	const perone_ui_api *api;
	perone_ui_callbacks callbacks;
	unsigned width, height;
	double *shown;
	int ready, closing, error;
} PeroneUI;

static const char *bindir(void *handle) {
	PeroneUI *ui = handle;
	return module_bindir(ui->session->nodes[ui->node].dsp[0].dsp->module);
}

static const char *datadir(void *handle) {
	PeroneUI *ui = handle;
	return module_datadir(ui->session->nodes[ui->node].dsp[0].dsp->module);
}

static void parameter(void *handle, size_t index, float value) {
	PeroneUI *ui = handle;
	if (ui->closing)
		return;
	const Node *node = session_score(ui->session)->nodes + ui->node;
	double adjusted = value;
	if (plugin_parameter(&node->config, index, &adjusted)) {
		ui->error = 1;
		return;
	}
	edit_dsp(ui->session->nodes[ui->node].dsp[0].dsp, index, adjusted);
}

static void message(void *handle, size_t size, const void *data) {
	PeroneUI *ui = handle;
	if (!ui->closing && send_dsp(ui->session->nodes[ui->node].dsp[0].dsp, size, data))
		ui->error = 1;
}

static void perone_close(void *view);

static int perone_open_ui(void **out, Session *session, int id) {
	*out = NULL;
	const Node *node = session_score(session)->nodes + id;
	if (!node->info.ui || (access(node->info.ui, F_OK) && errno == ENOENT))
		return 0;
	PeroneUI *ui = calloc(1, sizeof(*ui));
	if (!ui)
		return -1;
	ui->session = session;
	ui->node = id;
	ui->shown = calloc(node->config.nparams, sizeof(double));
	if (node->config.nparams && !ui->shown) {
		free(ui);
		return -1;
	}
	ui->library = dlopen(node->info.ui, RTLD_NOW | RTLD_LOCAL);
	const char *error;
	if (!ui->library) {
		error = dlerror();
		goto fail;
	}
	const perone_ui_api *(*get_api)(uint32_t) = dlsym(ui->library, "perone_ui_get_api");
	error = "unsupported Perone UI ABI";
	if (!get_api || !(ui->api = get_api(PERONE_UI_ABI_VERSION)))
		goto fail;
	const perone_ui_api *a = ui->api;
	error = "missing Perone UI function";
	if (!a->get_default_size || !a->create || !a->free || !a->idle || !a->get_widget ||
	    (node->config.nparams && !a->set_parameter) || (node->config.to_ui && !a->msg_in))
		goto fail;
	error = "cannot open X11 display";
	ui->display = XOpenDisplay(NULL);
	if (!ui->display)
		goto fail;
	uint32_t width, height;
	a->get_default_size(&width, &height);
	error = "invalid UI size";
	if (!width || !height || width > 16384 || height > 16384)
		goto fail;
	ui->width = width;
	ui->height = height;
	ui->parent = XCreateSimpleWindow(ui->display, DefaultRootWindow(ui->display), 0, 0, width, height, 0, 0, 0);
	XStoreName(ui->display, ui->parent, node->info.name);
	XSelectInput(ui->display, ui->parent, StructureNotifyMask | SubstructureNotifyMask);
	ui->quit = XInternAtom(ui->display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(ui->display, ui->parent, &ui->quit, 1);
	if (!node->info.resizable) {
		XSizeHints hints = {.flags = PMinSize | PMaxSize,
		    .min_width = width,
		    .max_width = width,
		    .min_height = height,
		    .max_height = height};
		XSetWMNormalHints(ui->display, ui->parent, &hints);
	}
	XSync(ui->display, False);
	ui->callbacks = (perone_ui_callbacks){ui, bindir, datadir, parameter, parameter, parameter, message};
	error = "cannot create Perone UI";
	ui->instance = a->create(PERONE_UI_X11, 1, (void *)(uintptr_t)ui->parent, &ui->callbacks);
	if (!ui->instance || !(ui->widget = (Window)(uintptr_t)a->get_widget(ui->instance)))
		goto fail;
	for (int i = 0; i < node->config.nparams; ++i) {
		ui->shown[i] = node->config.parameters[i].value;
		read_dsp(session->nodes[id].dsp[0].dsp, i, &ui->shown[i]);
		a->set_parameter(ui->instance, i, ui->shown[i]);
	}
	watch_dsp(session->nodes[id].dsp[0].dsp, 1);
	XFlush(ui->display);
	*out = ui;
	return 0;
fail:
	fprintf(stderr, "[UI] %s: %s\n", node->path, error);
	perone_close(ui);
	return -1;
}

static void perone_show(void *view, int visible) {
	PeroneUI *ui = view;
	if (!ui)
		return;
	if (visible)
		XMapWindow(ui->display, ui->parent);
	else
		XUnmapWindow(ui->display, ui->parent);
	XFlush(ui->display);
}

static int perone_poll(void *view) {
	PeroneUI *ui = view;
	if (!ui)
		return 0;
	const Node *node = session_score(ui->session)->nodes + ui->node;
	int resize = 0;
	while (XPending(ui->display)) {
		XEvent event;
		XNextEvent(ui->display, &event);
		if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == ui->quit)
			return 1;
		// The plugin may create its widget on another X connection. Wait for the server's event.
		if ((event.type == CreateNotify && event.xcreatewindow.window == ui->widget) ||
		    (event.type == ReparentNotify && event.xreparent.window == ui->widget &&
		        event.xreparent.parent == ui->parent)) {
			ui->ready = resize = 1;
			XMapWindow(ui->display, ui->widget);
		}
		if (event.type == ConfigureNotify && event.xconfigure.window == ui->parent) {
			ui->width = event.xconfigure.width;
			ui->height = event.xconfigure.height;
			resize = 1;
		}
	}
	if (resize && ui->ready && node->info.resizable)
		XResizeWindow(ui->display, ui->widget, ui->width, ui->height);
	XFlush(ui->display);
	DSP *first = ui->session->nodes[ui->node].dsp[0].dsp;
	for (int i = 0; i < node->config.nparams; ++i) {
		double value;
		if (read_dsp(first, i, &value) && isfinite(value) && value != ui->shown[i]) {
			ui->api->set_parameter(ui->instance, i, value);
			ui->shown[i] = value;
		}
	}
	size_t size;
	unsigned char data[MAX_MESSAGE];
	for (int n = 0; n < MESSAGE_SLOTS; ++n) {
		int result = receive_dsp(first, &size, data);
		if (result <= 0) {
			ui->error |= result < 0;
			break;
		}
		ui->api->msg_in(ui->instance, size, data);
	}
	ui->api->idle(ui->instance);
	if (ui->error)
		fprintf(stderr, "[UI] %s: invalid control or message queue overflow\n", node->info.name);
	return ui->error ? -1 : 0;
}

static void perone_close(void *view) {
	PeroneUI *ui = view;
	if (!ui)
		return;
	ui->closing = 1;
	watch_dsp(ui->session->nodes[ui->node].dsp[0].dsp, 0);
	if (ui->instance)
		ui->api->free(ui->instance);
	if (ui->parent)
		XDestroyWindow(ui->display, ui->parent);
	if (ui->display)
		XCloseDisplay(ui->display);
	if (ui->library)
		dlclose(ui->library);
	free(ui->shown);
	free(ui);
}

const UIOps perone_ui_ops = {perone_open_ui, perone_show, perone_poll, perone_close};
