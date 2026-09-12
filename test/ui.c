#include "../posix/ui.c"
#include <assert.h>
#include <time.h>

static void pump(UI *ui) {
	for (int i = 0; i < 50; ++i) {
		assert(!ui_poll(ui));
		nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
	}
	XSync(ui->display, False);
}

static void idle(void *instance) {
	(void)instance;
}

static void test_delayed_widget(void) {
	DSP dsp = {0};
	Node node = {0};
	node.dsp[0].dsp = &dsp;
	const perone_ui_api api = {.idle = idle};
	UI ui = {.node = &node, .api = &api, .resizable = 1, .width = 100, .height = 100};
	ui.display = XOpenDisplay(NULL);
	Display *plugin = XOpenDisplay(NULL);
	assert(ui.display && plugin);
	ui.parent = XCreateSimpleWindow(ui.display, DefaultRootWindow(ui.display), 0, 0, 100, 100, 0, 0, 0);
	XChangeWindowAttributes(
	    ui.display, ui.parent, CWOverrideRedirect, &(XSetWindowAttributes){.override_redirect = True});
	XSelectInput(ui.display, ui.parent, StructureNotifyMask | SubstructureNotifyMask);
	XSync(ui.display, False);
	// Keep the child's creation buffered on a different connection while the host receives a resize.
	ui.widget = XCreateSimpleWindow(plugin, ui.parent, 0, 0, 100, 100, 0, 0, 0);
	XResizeWindow(ui.display, ui.parent, 200, 300);
	XSync(ui.display, False);
	assert(!ui_poll(&ui) && !ui.ready && ui.width == 200 && ui.height == 300);
	XSync(ui.display, False); // Mapping/resizing the missing child here would cause BadWindow.
	XFlush(plugin);
	pump(&ui);
	XWindowAttributes attributes;
	assert(ui.ready && XGetWindowAttributes(ui.display, ui.widget, &attributes));
	assert(attributes.width == 200 && attributes.height == 300 && attributes.map_state == IsUnviewable);
	XDestroyWindow(plugin, ui.widget);
	XSync(plugin, False);
	XCloseDisplay(plugin);
	XDestroyWindow(ui.display, ui.parent);
	XCloseDisplay(ui.display);
	puts("OK: widget creation on a separate X connection, deferred mapping and resize");
}

static void mouse(UI *ui, int type, int x, int y) {
	XEvent event = {0};
	event.xbutton.type = type;
	event.xbutton.display = ui->display;
	event.xbutton.window = ui->widget;
	event.xbutton.root = DefaultRootWindow(ui->display);
	event.xbutton.x = x;
	event.xbutton.y = y;
	event.xbutton.button = Button1;
	event.xbutton.state = type == MotionNotify ? Button1Mask : 0;
	event.xbutton.same_screen = True;
	assert(XSendEvent(ui->display, ui->widget, False,
	    type == MotionNotify      ? PointerMotionMask
	        : type == ButtonPress ? ButtonPressMask
	                              : ButtonReleaseMask,
	    &event));
	XSync(ui->display, False);
	pump(ui);
}

static void test_view(const char *bundle, int tibia) {
	Node node = {0};
	PluginConfig config;
	assert(!read_bundle(bundle, &node.path, &config));
	assert(!open_engine(node.dsp, node.path, &config, 48000));
	// A mono effect on stereo has two DSPs but one editor.
	assert(!open_engine(node.dsp + 1, node.path, &config, 48000));
	UI *ui;
	assert(!ui_open(&ui, &node) && ui);
	XUnmapWindow(ui->display, ui->parent);
	pump(ui);
	Window root, parent, *children;
	unsigned count;
	assert(XQueryTree(ui->display, ui->widget, &root, &parent, &children, &count));
	if (children)
		XFree(children);
	assert(parent == ui->parent);
	float value;
	for (int i = 0; i < config.nparams; ++i)
		assert(read_dsp(node.dsp[0].dsp, i, &value));
	float input[BLOCK], before[BLOCK], after[BLOCK], right[BLOCK];
	for (int i = 0; i < BLOCK; ++i)
		input[i] = i % 2 ? .1f : -.1f;
	render(node.dsp, before, input, BLOCK);
	render(node.dsp + 1, right, input, BLOCK);
	pump(ui);
	if (tibia) {
		mouse(ui, ButtonPress, 300, 65);
		mouse(ui, ButtonRelease, 300, 65);
	} else {
		mouse(ui, ButtonPress, 68, 120);
		mouse(ui, MotionNotify, 68, 205);
		mouse(ui, ButtonRelease, 68, 205);
	}
	DSP *dsp = node.dsp[0].dsp;
	assert(!read_dsp(dsp, 0, &value));
	assert(atomic_load(&dsp->values[0]) == config.defaults[0]); // The UI has not called the DSP.
	sync_dsp(dsp, node.dsp[1].dsp);
	render(node.dsp, after, input, BLOCK);
	render(node.dsp + 1, right, input, BLOCK);
	assert(!memcmp(after, right, sizeof(after)));
	assert(memcmp(after, before, sizeof(after)));
	assert(atomic_load(&dsp->values[0]) < config.defaults[0]);
	pump(ui);
	assert(ui->shown[0] == atomic_load(&dsp->values[0]));
	int output = tibia ? 5 : 4;
	assert(ui->shown[output] == dsp->api->get_parameter(dsp->instance, output));
	// An edit arriving between L and R must wait for the next shared block boundary.
	render(node.dsp, after, input, BLOCK);
	ui->callbacks.set_parameter(ui, 0, config.defaults[0]);
	render(node.dsp + 1, right, input, BLOCK);
	assert(!memcmp(after, right, sizeof(after)) && !read_dsp(dsp, 0, &value));
	sync_dsp(dsp, node.dsp[1].dsp);
	assert(atomic_load(&dsp->values[0]) == config.defaults[0]);
	assert(atomic_load(&node.dsp[1].dsp->values[0]) == config.defaults[0]);
	pump(ui);
	// A scheduled parameter reaches both the DSP and UI at its sample boundary, without a gesture.
	Event automation = {.time = node.dsp[0].time + 31, .parameter = 0, .value = tibia ? -12 : 60};
	for (int i = 0; i < 2; ++i) {
		node.dsp[i].events = &automation;
		node.dsp[i].count = 1;
		render(node.dsp + i, after, input, 31);
	}
	pump(ui);
	assert(ui->shown[0] == config.defaults[0]);
	for (int i = 0; i < 2; ++i) {
		render(node.dsp + i, after, input, 1);
		assert(atomic_load(&node.dsp[i].dsp->values[0]) == automation.value);
		assert(read_dsp(node.dsp[i].dsp, 0, &value) && value == automation.value);
	}
	pump(ui);
	assert(ui->shown[0] == automation.value);
	if (tibia) {
		mouse(ui, ButtonPress, 510, 438);
		mouse(ui, ButtonRelease, 510, 438);
		assert(atomic_load(&dsp->to_dsp.write) - atomic_load(&dsp->to_dsp.read) == 1);
		sync_dsp(dsp, node.dsp[1].dsp);
		render(node.dsp, after, input, BLOCK);
		// The GUI's reset message reaches the DSP, whose reply starts counting again.
		char data[MAX_MESSAGE + 1];
		size_t size;
		assert(receive_dsp(dsp, &size, data) == 1);
		data[size] = 0;
		assert(fabs(strtod(data, NULL) - .01) < .001);
		assert(!message_push(&dsp->to_ui, size, data));
		pump(ui);
		assert(atomic_load(&dsp->to_ui.read) == atomic_load(&dsp->to_ui.write));
	}
	XResizeWindow(ui->display, ui->parent, 808, 568);
	XSync(ui->display, False);
	pump(ui);
	XWindowAttributes attributes;
	assert(XGetWindowAttributes(ui->display, ui->widget, &attributes));
	assert(attributes.width == 808 && attributes.height == 568);
	// Window-manager close is handled by the host, then UI and DSP are freed in order.
	XEvent close = {0};
	close.xclient.type = ClientMessage;
	close.xclient.window = ui->parent;
	close.xclient.format = 32;
	close.xclient.message_type = XInternAtom(ui->display, "WM_PROTOCOLS", False);
	close.xclient.data.l[0] = ui->quit;
	assert(XSendEvent(ui->display, ui->parent, False, NoEventMask, &close));
	XSync(ui->display, False);
	assert(ui_poll(ui) == 1);
	ui_close(ui);
	assert(!atomic_load(&dsp->viewing));
	close_engine(node.dsp);
	close_engine(node.dsp + 1);
	free(node.path);
	printf("OK: %s: original UI gestures, audio, stereo copies, automation feedback, resize and close\n", bundle);
}

int main(void) {
	test_delayed_widget();
	test_view("../tibia/out/perone/c/build/tibia-test.perone", 1);
	test_view("../tibia/out/perone/cxx/build/tibia-test.perone", 1);
	test_view("../asid/plugin/perone/build/asid.perone", 0);
	return 0;
}
