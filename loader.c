#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>

Module *load_module(const char *path, const PluginConfig *config) {
	Module *m = calloc(1, sizeof(*m));
	if (!m) return NULL;
	const char *error = "invalid plugin configuration";
	if (config->input < 0 || config->input > 2 || config->output < 1 || config->output > 2
		|| config->inputs < config->input || config->inputs > MAX_INPUTS || config->input_offset < 0 || config->input_offset > MAX_INPUTS
		|| config->input_offset + config->input > config->inputs || config->midi < -1
		|| config->nparams < 0 || config->nparams > MAX_PARAMS) goto fail;
	m->config = *config;
	error = "cannot resolve plugin binary";
	m->bindir = realpath(path, NULL);
	if (!m->bindir) goto fail;
	*strrchr(m->bindir, '/') = 0;
	m->datadir = strdup(m->bindir);
	if (!m->datadir) { error = "out of memory"; goto fail; }
	char *slash = strrchr(m->datadir, '/');
	if (!slash) goto fail;
	*slash = 0;
	m->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!m->handle) { error = dlerror(); goto fail; }
	const perone_api *(*get_api)(uint32_t) = dlsym(m->handle, "perone_get_api");
	if (!get_api || !(m->api = get_api(PERONE_ABI_VERSION))) { error = "unsupported Perone ABI"; goto fail; }
	const perone_api *a = m->api;
	error = "missing Perone function";
	if (!a->alloc || !a->free || !a->init || !a->fini || !a->set_sample_rate
		|| !a->mem_req || !a->mem_set || !a->reset || !a->process || (config->midi >= 0 && !a->midi_msg_in)) goto fail;
	for (int i = 0; i < config->nparams; ++i)
		if ((config->outputs & (UINT64_C(1) << i)) ? !a->get_parameter
			: (!a->set_parameter || !isfinite(config->defaults[i]))) goto fail;
	return m;
fail:
	fprintf(stderr, "[Loader] %s: %s\n", path, error);
	unload_module(m);
	return NULL;
}

void unload_module(Module *m) {
	if (m) { if (m->handle) dlclose(m->handle); free(m->bindir); free(m->datadir); free(m); }
}
