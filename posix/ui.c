#include "ui.h"
#include "module.h"
#include "perone_ui.h"
#include "script.h"
#include "util.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct UI {
	Node *node;
	Display *display;
	Window parent, widget;
	Atom quit;
	void *library, *instance;
	const perone_ui_api *api;
	perone_ui_callbacks callbacks;
	char *path, *name;
	unsigned width, height;
	float low[MAX_PARAMS], high[MAX_PARAMS], shown[MAX_PARAMS];
	int integer[MAX_PARAMS], resizable, ready, error;
};

static const char *bindir(void *handle) {
	return ((UI *)handle)->node->dsp[0].dsp->bindir;
}

static const char *datadir(void *handle) {
	return ((UI *)handle)->node->dsp[0].dsp->datadir;
}

static void parameter(void *handle, size_t index, float value) {
	UI *ui = handle;
	const PluginConfig *config = &ui->node->dsp[0].config;
	if (index >= (size_t)config->nparams || (config->outputs & (UINT64_C(1) << index)) || !isfinite(value)) {
		ui->error = 1;
		return;
	}
	if (ui->integer[index])
		value = roundf(value);
	value = fminf(ui->high[index], fmaxf(ui->low[index], value));
	edit_dsp(ui->node->dsp[0].dsp, index, value);
}

static void message(void *handle, size_t size, const void *data) {
	UI *ui = handle;
	if (send_dsp(ui->node->dsp[0].dsp, size, data))
		ui->error = 1;
}

static float field(Janet value, const char *key) {
	Janet x = janet_get(value, janet_ckeywordv(key));
	return janet_getnumber(&x, 0);
}

static Janet configure(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 4);
	UI *ui = janet_getpointer(argv, 0);
	Janet metadata = janet_get(argv[1], janet_ckeywordv("ui"));
	if (janet_checktype(metadata, JANET_NIL))
		return janet_wrap_nil();
	ui->path = copy_string(janet_getcstring(argv, 3));
	Janet name = janet_get(argv[1], janet_ckeywordv("name"));
	ui->name = copy_string(janet_checktype(name, JANET_STRING) ? (const char *)janet_unwrap_string(name) : "Perone");
	if (!ui->path || !ui->name)
		janet_panic("out of memory");
	ui->resizable = janet_truthy(janet_get(metadata, janet_ckeywordv("userResizable")));
	JanetView parameters = janet_getindexed(&argv[2], 0);
	for (int i = 0; i < parameters.len; ++i) {
		Janet p = parameters.items[i];
		ui->integer[i] = janet_truthy(janet_get(p, janet_ckeywordv("integer")));
		ui->low[i] = ui->integer[i] ? ceilf(field(p, "min")) : field(p, "min");
		ui->high[i] = ui->integer[i] ? floorf(field(p, "max")) : field(p, "max");
	}
	return janet_wrap_nil();
}

int ui_open(UI **out, Node *node) {
	*out = NULL;
	if (!node->path)
		return 0;
	UI *ui = calloc(1, sizeof(*ui));
	if (!ui)
		return -1;
	ui->node = node;
	JanetTable *env = script_env();
	const char *error = "cannot read UI metadata";
	if (!env)
		goto fail;
	janet_def(env, "host/ui", janet_wrap_cfunction(configure), NULL);
	janet_def(env, "host/view", janet_wrap_pointer(ui), NULL);
	janet_def(env, "host/bundle", janet_cstringv(datadir(ui)), NULL);
	int result = janet_dostring(env,
	    "(def p (perone/read host/bundle)) "
	    "(host/ui host/view (p :product) (p :parameters) (p :ui-binary))",
	    "UI metadata", NULL);
	janet_deinit();
	if (result)
		goto fail;
	if (!ui->path || (access(ui->path, F_OK) && errno == ENOENT)) {
		ui_close(ui);
		return 0;
	}
	ui->library = dlopen(ui->path, RTLD_NOW | RTLD_LOCAL);
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
	    (node->dsp[0].config.nparams && !a->set_parameter) || (node->dsp[0].config.to_ui && !a->msg_in))
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
	XStoreName(ui->display, ui->parent, ui->name);
	XSelectInput(ui->display, ui->parent, StructureNotifyMask | SubstructureNotifyMask);
	ui->quit = XInternAtom(ui->display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(ui->display, ui->parent, &ui->quit, 1);
	if (!ui->resizable) {
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
	for (int i = 0; i < node->dsp[0].config.nparams; ++i) {
		ui->shown[i] = node->dsp[0].config.defaults[i];
		read_dsp(node->dsp[0].dsp, i, &ui->shown[i]);
		a->set_parameter(ui->instance, i, ui->shown[i]);
	}
	watch_dsp(node->dsp[0].dsp, 1);
	XMapWindow(ui->display, ui->parent);
	XFlush(ui->display);
	*out = ui;
	return 0;
fail:
	fprintf(stderr, "[UI] %s: %s\n", node->path, error);
	ui_close(ui);
	return -1;
}

int ui_poll(UI *ui) {
	if (!ui)
		return 0;
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
	if (resize && ui->ready && ui->resizable)
		XResizeWindow(ui->display, ui->widget, ui->width, ui->height);
	XFlush(ui->display);
	DSP *first = ui->node->dsp[0].dsp;
	for (int i = 0; i < ui->node->dsp[0].config.nparams; ++i) {
		float value;
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
		fprintf(stderr, "[UI] %s: invalid control or message queue overflow\n", ui->name);
	return ui->error ? -1 : 0;
}

void ui_close(UI *ui) {
	if (!ui)
		return;
	watch_dsp(ui->node->dsp[0].dsp, 0);
	if (ui->instance)
		ui->api->free(ui->instance);
	if (ui->parent)
		XDestroyWindow(ui->display, ui->parent);
	if (ui->display)
		XCloseDisplay(ui->display);
	if (ui->library)
		dlclose(ui->library);
	free(ui->path);
	free(ui->name);
	free(ui);
}
