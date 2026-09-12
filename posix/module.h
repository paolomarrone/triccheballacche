#ifndef MODULE_H
#define MODULE_H
#include "loader.h"
#include "perone.h"

// POSIX loader internals. Only posix/loader.c and tests constructing native fixtures use this layout.
struct DSP {
	void *handle, *instance, *memory;
	const perone_api *api;
	char *bindir, *datadir;
	int initialized;
};
#endif
