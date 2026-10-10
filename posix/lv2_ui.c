#include "lv2.h"
#include "ui_internal.h"
#include "window_x11.h"
#include <dlfcn.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	Lv2 *lv2;
	void *library;
	const LV2UI_Descriptor *api;
	LV2UI_Handle handle;
	const LV2UI_Idle_Interface *idle;
	const LV2UI_Resize *resize;
	PluginWindow window;
	LV2UI_Widget widget;
	int failed, sized;
	double *shown;
	LV2UI_Resize host_resize;
	LV2UI_Port_Map port_map;
	LV2_Extension_Data_Feature access;
	LV2_Feature features[8];
	const LV2_Feature *feature_ptrs[17];
} Lv2UI;

static int resize(void *handle, int width, int height) {
	Lv2UI *ui = handle;
	ui->sized = 1;
	return window_size(&ui->window, width, height);
}
static uint32_t port_index(void *handle, const char *symbol) {
	Lv2UI *ui = handle;
	LilvNode *name = lilv_new_string(ui->lv2->description.world, symbol);
	const LilvPort *p = lilv_plugin_get_port_by_symbol(ui->lv2->description.plugin, name);
	lilv_node_free(name);
	return p ? lilv_port_get_index(ui->lv2->description.plugin, p) : LV2UI_INVALID_PORT_INDEX;
}
static void write_port(void *handle, uint32_t port, uint32_t size, uint32_t protocol, const void *data) {
	Lv2UI *ui = handle;
	Lv2 *l = ui->lv2;
	if (port >= l->description.count) {
		ui->failed = 1;
		return;
	}
	Lv2Port *p = l->description.ports + port;
	if (!protocol && p->input && p->type == LV2_PORT_CONTROL && size == sizeof(float)) {
		float number;
		memcpy(&number, data, sizeof(number));
		double value = number;
		if (!plugin_parameter(&l->dsp->config, p->parameter, &value))
			edit_dsp(l->dsp, p->parameter, value);
	} else if (protocol == l->event_transfer) {
		if (lv2_ui_send(l, port, size, data))
			ui->failed = 1;
	} else
		ui->failed = 1;
}
static void show(void *view, int visible) {
	Lv2UI *ui = view;
	window_show(&ui->window, visible);
}
static void initial_size(Lv2UI *ui) {
	// The UI uses a separate X connection; its child may reach the server after instantiate().
	Window root, parent, *children = NULL;
	unsigned count = 0;
	if (!ui->sized && XQueryTree(ui->window.display, ui->window.parent, &root, &parent, &children, &count)) {
		for (unsigned i = 0; i < count; ++i)
			if (children[i] == (Window)(uintptr_t)ui->widget) {
				XWindowAttributes attr;
				if (XGetWindowAttributes(ui->window.display, children[i], &attr))
					resize(ui, attr.width, attr.height);
			}
		XFree(children);
	}
}
static void close_view(void *view) {
	Lv2UI *ui = view;
	if (!ui)
		return;
	if (ui->handle) {
		ui->api->cleanup(ui->handle);
		watch_dsp(ui->lv2->dsp, 0);
	}
	if (ui->library)
		dlclose(ui->library);
	window_close(&ui->window);
	free(ui->shown);
	free(ui);
}
static int open_view(void **out, Session *session, int id) {
	*out = NULL;
	Lv2 *l = session->nodes[id].dsp[0].dsp->instance;
	const LilvUI *selected;
	LilvUIs *uis = lv2_uis(&l->description, &selected);
	if (!selected) {
		lilv_uis_free(uis);
		return -1;
	}
	Lv2UI *ui = calloc(1, sizeof(*ui));
	char *binary = lilv_file_uri_parse(lilv_node_as_uri(lilv_ui_get_binary_uri(selected)), NULL);
	char *bundle = lilv_file_uri_parse(lilv_node_as_uri(lilv_ui_get_bundle_uri(selected)), NULL);
	if (!ui)
		goto fail;
	ui->lv2 = l;
	if (!binary || !bundle)
		goto fail;
	ui->library = dlopen(binary, RTLD_NOW | RTLD_LOCAL);
	if (!ui->library)
		goto fail;
	LV2UI_DescriptorFunction descriptor = (LV2UI_DescriptorFunction)dlsym(ui->library, "lv2ui_descriptor");
	if (!descriptor)
		goto fail;
	for (uint32_t i = 0; i < 256; ++i) {
		const LV2UI_Descriptor *api = descriptor(i);
		if (!api)
			break;
		if (!strcmp(api->URI, lilv_node_as_uri(lilv_ui_get_uri(selected)))) {
			ui->api = api;
			break;
		}
	}
	if (!ui->api || !ui->api->instantiate || !ui->api->cleanup)
		goto fail;
	if (window_open(&ui->window, session_score(session)->nodes[id].info.name, 480, 240,
	        lv2_ui_fixed(&l->description, selected)))
		goto fail;
	ui->host_resize = (LV2UI_Resize){ui, resize};
	ui->port_map = (LV2UI_Port_Map){ui, port_index};
	ui->access.data_access = lilv_instance_get_descriptor(l->instance)->extension_data;
	ui->features[0] = (LV2_Feature){LV2_UI__parent, (void *)(uintptr_t)ui->window.parent};
	ui->features[1] = (LV2_Feature){LV2_UI__resize, &ui->host_resize};
	ui->features[2] = (LV2_Feature){LV2_UI__idleInterface, NULL};
	ui->features[3] = (LV2_Feature){LV2_INSTANCE_ACCESS_URI, lilv_instance_get_handle(l->instance)};
	ui->features[4] = (LV2_Feature){LV2_DATA_ACCESS_URI, &ui->access};
	ui->features[5] = (LV2_Feature){LV2_UI__portMap, &ui->port_map};
	ui->features[6] = (LV2_Feature){LV2_UI__fixedSize, NULL};
	ui->features[7] = (LV2_Feature){LV2_UI__noUserResize, NULL};
	int count = 0;
	for (; l->feature_ptrs[count]; ++count)
		ui->feature_ptrs[count] = l->feature_ptrs[count];
	for (int i = 0; i < 8; ++i)
		ui->feature_ptrs[count++] = ui->features + i;
	ui->handle =
	    ui->api->instantiate(ui->api, l->dsp->config.id, bundle, write_port, ui, &ui->widget, ui->feature_ptrs);
	if (!ui->handle || !ui->widget)
		goto fail;
	if (ui->api->extension_data) {
		ui->idle = ui->api->extension_data(LV2_UI__idleInterface);
		ui->resize = ui->api->extension_data(LV2_UI__resize);
	}
	initial_size(ui);
	ui->shown = malloc(l->dsp->config.nparams * sizeof(double));
	if (l->dsp->config.nparams && !ui->shown)
		goto fail;
	for (int i = 0; i < l->dsp->config.nparams; ++i)
		ui->shown[i] = NAN;
	atomic_store(&l->to_ui.read, atomic_load(&l->to_ui.write));
	watch_dsp(l->dsp, 1);
	free(binary);
	free(bundle);
	lilv_uis_free(uis);
	*out = ui;
	return 0;
fail:
	free(binary);
	free(bundle);
	lilv_uis_free(uis);
	close_view(ui);
	return -1;
}
static int resized(void *view, unsigned *width, unsigned *height) {
	Lv2UI *ui = view;
	return !ui->resize || ui->resize->ui_resize(ui->handle, *width, *height);
}
static int poll_view(void *view) {
	Lv2UI *ui = view;
	Lv2 *l = ui->lv2;
	if (ui->failed || atomic_load(&l->dsp->overflow))
		return -1;
	initial_size(ui);
	if (window_poll(&ui->window, resized, ui))
		return 1;
	for (int i = 0; ui->api->port_event && i < l->dsp->config.nparams; ++i) {
		double value;
		if (!read_dsp(l->dsp, i, &value) || value == ui->shown[i])
			continue;
		ui->shown[i] = value;
		uint32_t port = l->dsp->config.parameters[i].id;
		float number = value;
		ui->api->port_event(ui->handle, port, sizeof(number), 0, &number);
	}
	uint64_t data[MAX_MESSAGE / sizeof(uint64_t)];
	size_t size;
	for (int i = 0; i < MESSAGE_SLOTS && message_pop(&l->to_ui, &size, data); ++i) {
		uint32_t port = data[0];
		if (ui->api->port_event)
			ui->api->port_event(ui->handle, port, size - sizeof(uint64_t), l->event_transfer, data + 1);
	}
	return ui->idle && ui->idle->idle(ui->handle) ? 1 : 0;
}
const UIOps lv2_ui_ops = {open_view, show, poll_view, close_view};
