#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

TibiaModule* tibia_loader_load(const char *path) {
	TibiaModule *mod = (TibiaModule*)malloc(sizeof(TibiaModule));
	if (!mod) return NULL;

	mod->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

	if (!mod->handle) {
		fprintf(stderr, "[Loader] Error loading %s: %s\n", path, dlerror());
		free(mod);
		return NULL;
	}

	#define LOAD_SYM(name) \
		mod->name = dlsym(mod->handle, "tibia_"#name); \
		if (!mod->name) { \
			fprintf(stderr, "[Loader] Missing symbol: %s\n", #name); \
			dlclose(mod->handle); \
			free(mod); \
			return NULL; \
		}

	LOAD_SYM(init);
	LOAD_SYM(fini);
	LOAD_SYM(set_sample_rate);
	LOAD_SYM(mem_req);
	LOAD_SYM(mem_set);
	LOAD_SYM(reset);
	LOAD_SYM(set_parameter);
	LOAD_SYM(get_parameter);
	LOAD_SYM(process);
	LOAD_SYM(midi_msg_in);
	LOAD_SYM(state_save);
	LOAD_SYM(state_load);

	return mod;
}

void tibia_loader_unload(TibiaModule *mod) {
	if (mod) {
		if (mod->handle) {
			dlclose(mod->handle);
		}
		free(mod);
	}
}