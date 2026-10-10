#include "host/vst3.h"
#include "host/platform.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>

// Linux VST3 bundles have one binary per architecture and boolean entry points.
void *vst_library_open(const char *path) {
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	size_t length = strlen(name);
	struct utsname system;
	if (length <= 5 || strcmp(name + length - 5, ".vst3") || uname(&system))
		return NULL;
	size_t size = strlen(path) + length + strlen(system.machine) + 32;
	char *binary = malloc(size);
	if (!binary)
		return NULL;
	snprintf(binary, size, "%s/Contents/%s-linux/%.*s.so", path, system.machine, (int)length - 5, name);
	void *library = library_open(binary);
	free(binary);
	char (*entry)(void *) = library ? library_symbol(library, "ModuleEntry") : NULL;
	char (*leave)(void) = library ? library_symbol(library, "ModuleExit") : NULL;
	if (!entry || !leave || !entry(library)) {
		library_close(library);
		return NULL;
	}
	return library;
}
void vst_library_close(void *library) {
	if (!library)
		return;
	char (*leave)(void) = library_symbol(library, "ModuleExit");
	if (!leave())
		fputs("[VST3] ModuleExit reported failure\n", stderr);
	library_close(library);
}

typedef struct {
	Steinberg_Linux_IEventHandler *handler;
	int fd;
	uint64_t serial;
} Event;
typedef struct {
	Steinberg_Linux_ITimerHandler *handler;
	uint64_t period, next, serial;
} Timer;
typedef struct {
	Steinberg_Linux_IRunLoop api;
	unsigned refs, nevents, ntimers;
	uint64_t serial;
	Event events[128];
	Timer timers[128];
} Loop;
static Steinberg_tresult SMTG_STDMETHODCALLTYPE query(void *s, const Steinberg_TUID iid, void **out) {
	return vst_match(s, iid, Steinberg_Linux_IRunLoop_iid, out);
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE retain(void *s) {
	return ++((Loop *)s)->refs;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE release(void *s) {
	Loop *l = s;
	if (--l->refs)
		return l->refs;
	for (unsigned i = 0; i < l->nevents; ++i)
		vst_release(l->events[i].handler);
	for (unsigned i = 0; i < l->ntimers; ++i)
		vst_release(l->timers[i].handler);
	free(l);
	return 0;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE register_event(
    void *s, Steinberg_Linux_IEventHandler *handler, int fd) {
	Loop *l = s;
	if (!handler || fd < 0 || l->nevents == 128)
		return Steinberg_kInvalidArgument;
	for (unsigned i = 0; i < l->nevents; ++i)
		if (l->events[i].handler == handler && l->events[i].fd == fd)
			return Steinberg_kResultFalse;
	vst_retain(handler);
	l->events[l->nevents++] = (Event){handler, fd, ++l->serial};
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE unregister_event(void *s, Steinberg_Linux_IEventHandler *handler) {
	Loop *l = s;
	int found = 0;
	for (unsigned i = 0; i < l->nevents;) {
		if (l->events[i].handler != handler) {
			++i;
			continue;
		}
		l->events[i] = l->events[--l->nevents];
		vst_release(handler);
		found = 1;
	}
	return found ? Steinberg_kResultOk : Steinberg_kResultFalse;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE register_timer(
    void *s, Steinberg_Linux_ITimerHandler *handler, Steinberg_uint64 period) {
	Loop *l = s;
	if (!handler || !period || period > UINT32_MAX || l->ntimers == 128)
		return Steinberg_kInvalidArgument;
	for (unsigned i = 0; i < l->ntimers; ++i)
		if (l->timers[i].handler == handler)
			return Steinberg_kResultFalse;
	vst_retain(handler);
	l->timers[l->ntimers++] = (Timer){handler, period, host_clock() + period, ++l->serial};
	return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE unregister_timer(void *s, Steinberg_Linux_ITimerHandler *handler) {
	Loop *l = s;
	for (unsigned i = 0; i < l->ntimers; ++i)
		if (l->timers[i].handler == handler) {
			l->timers[i] = l->timers[--l->ntimers];
			vst_release(handler);
			return Steinberg_kResultOk;
		}
	return Steinberg_kResultFalse;
}
void *vst_loop_new(void) {
	static Steinberg_Linux_IRunLoopVtbl table = {
	    query, retain, release, register_event, unregister_event, register_timer, unregister_timer};
	Loop *l = calloc(1, sizeof(*l));
	if (l) {
		l->api.lpVtbl = &table;
		l->refs = 1;
	}
	return l;
}
void vst_loop_poll(void *loop) {
	Loop *l = loop;
	struct pollfd fds[128];
	Event events[128];
	Timer timers[128];
	unsigned nevents = l->nevents, ntimers = l->ntimers;
	memcpy(events, l->events, nevents * sizeof(*events));
	memcpy(timers, l->timers, ntimers * sizeof(*timers));
	for (unsigned i = 0; i < nevents; ++i) {
		fds[i] = (struct pollfd){.fd = events[i].fd, .events = POLLIN};
		vst_retain(events[i].handler);
	}
	for (unsigned i = 0; i < ntimers; ++i)
		vst_retain(timers[i].handler);
	int ready = poll(fds, nevents, 0);
	for (unsigned i = 0; i < nevents; ++i) {
		if (ready > 0 && fds[i].revents)
			for (unsigned j = 0; j < l->nevents; ++j)
				if (events[i].serial == l->events[j].serial) {
					events[i].handler->lpVtbl->onFDIsSet(events[i].handler, events[i].fd);
					break;
				}
		vst_release(events[i].handler);
	}
	uint64_t now = host_clock();
	for (unsigned i = 0; i < ntimers; ++i) {
		if (now >= timers[i].next)
			for (unsigned j = 0; j < l->ntimers; ++j)
				if (timers[i].serial == l->timers[j].serial) {
					l->timers[j].next = now + timers[i].period;
					timers[i].handler->lpVtbl->onTimer(timers[i].handler);
					break;
				}
		vst_release(timers[i].handler);
	}
}
