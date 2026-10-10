#include "host/ui_internal.h"
#include "host/vst3.h"
#include "window.h"
#include <stdlib.h>

typedef struct {
	Steinberg_IPlugFrame frame;
	Vst *vst;
	Steinberg_IPlugView *view;
	PluginWindow window;
	int attached;
} VstUI;
static Steinberg_tresult SMTG_STDMETHODCALLTYPE query(void *s, const Steinberg_TUID iid, void **out) {
	VstUI *ui = s;
	if (!memcmp(iid, Steinberg_Linux_IRunLoop_iid, 16)) {
		*out = vst_query(vst_host_context(vst_host(ui->vst->module)), iid);
		return *out ? Steinberg_kResultOk : Steinberg_kNoInterface;
	}
	return vst_match(s, iid, Steinberg_IPlugFrame_iid, out);
}
static int dimensions(const struct Steinberg_ViewRect *rect, unsigned *width, unsigned *height) {
	int64_t w = (int64_t)rect->right - rect->left, h = (int64_t)rect->bottom - rect->top;
	if (w < 1 || h < 1 || w > 16384 || h > 16384)
		return -1;
	*width = w;
	*height = h;
	return 0;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE resize(
    void *s, Steinberg_IPlugView *view, struct Steinberg_ViewRect *rect) {
	VstUI *ui = s;
	unsigned width, height;
	if (view != ui->view || !rect || dimensions(rect, &width, &height) || window_size(&ui->window, width, height))
		return Steinberg_kInvalidArgument;
	return view->lpVtbl->onSize(view, rect);
}
static void show(void *view, int visible) {
	VstUI *ui = view;
	window_show(&ui->window, visible);
	ui->view->lpVtbl->onFocus(ui->view, visible);
}
static void close_view(void *view) {
	VstUI *ui = view;
	if (!ui)
		return;
	if (ui->view) {
		if (ui->attached)
			ui->view->lpVtbl->removed(ui->view);
		ui->view->lpVtbl->setFrame(ui->view, NULL);
		vst_release(ui->view);
	}
	window_close(&ui->window);
	free(ui);
}
static int open_view(void **out, Session *session, int id) {
	*out = NULL;
	Vst *v = session->nodes[id].dsp[0].dsp->instance;
	if (!v->controller)
		return -1;
	VstUI *ui = calloc(1, sizeof(*ui));
	if (!ui)
		return -1;
	static Steinberg_IPlugFrameVtbl table = {query, vst_borrow, vst_borrow, resize};
	ui->frame.lpVtbl = &table;
	ui->vst = v;
	vst_update_controller(v);
	ui->view = v->controller->lpVtbl->createView(v->controller, "editor");
	Steinberg_IPlugView *p = ui->view;
	unsigned width, height;
	struct Steinberg_ViewRect rect;
	if (!p || p->lpVtbl->isPlatformTypeSupported(p, vst_window_type) != Steinberg_kResultOk ||
	    p->lpVtbl->getSize(p, &rect) != Steinberg_kResultOk || dimensions(&rect, &width, &height) ||
	    window_open(&ui->window, session_score(session)->nodes[id].info.name, width, height,
	        p->lpVtbl->canResize(p) != Steinberg_kResultOk))
		goto fail;
	if (p->lpVtbl->setFrame(p, &ui->frame) != Steinberg_kResultOk ||
	    p->lpVtbl->attached(p, (void *)(uintptr_t)ui->window.parent, vst_window_type) != Steinberg_kResultOk)
		goto fail;
	ui->attached = 1;
	*out = ui;
	return 0;
fail:
	close_view(ui);
	return -1;
}
static int resized(void *view, unsigned *width, unsigned *height) {
	VstUI *ui = view;
	struct Steinberg_ViewRect rect = {0, 0, *width, *height};
	ui->view->lpVtbl->checkSizeConstraint(ui->view, &rect);
	return dimensions(&rect, width, height) || ui->view->lpVtbl->onSize(ui->view, &rect) != Steinberg_kResultOk;
}
static int poll_view(void *view) {
	VstUI *ui = view;
	return window_poll(&ui->window, resized, ui);
}
const UIOps vst3_ui_ops = {open_view, show, poll_view, close_view};
