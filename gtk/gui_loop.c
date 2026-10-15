#include "posix/gui_loop.h"
#include <dlfcn.h>
#include <stddef.h>
#include <time.h>

// WebUI loads GTK at runtime. Use its default context without adding a build dependency.
static void *library;
static int (*iterate)(void *, int);
static int (*remove_source)(unsigned);
static unsigned timer;
static int elapsed;

static int tick(void *data) {
	(void)data;
	elapsed = 1;
	return 1;
}

int gui_loop_init(void) {
	library = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
	if (!library)
		return -1;
	iterate = dlsym(library, "g_main_context_iteration");
	remove_source = dlsym(library, "g_source_remove");
	unsigned (*add_timer)(unsigned, int (*)(void *), void *) = dlsym(library, "g_timeout_add");
	if (iterate && remove_source && add_timer)
		timer = add_timer(16, tick, NULL);
	if (!timer) {
		gui_loop_free();
		return -1;
	}
	return 0;
}

void gui_loop_wait(void) {
	if (!timer) {
		nanosleep(&(struct timespec){.tv_nsec = 16000000}, NULL);
		return;
	}
	elapsed = 0;
	while (!elapsed)
		iterate(NULL, 1);
}

void gui_loop_free(void) {
	if (timer)
		remove_source(timer);
	timer = 0;
	if (library)
		dlclose(library);
	library = NULL;
}
