#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>

TibiaModule *tibia_loader_load(const char *path) {
	TibiaModule *m = calloc(1, sizeof(*m));
	if (!m) return NULL;
	m->midi = -1;
	const char *error = NULL;
	m->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!m->handle) { error = dlerror(); goto fail; }
	const tibia_api *(*get_api)(uint32_t) = dlsym(m->handle, "tibia_get_api");
	if (!get_api || !(m->api = get_api(TIBIA_ABI_VERSION))) { error = "unsupported Tibia shared ABI; rebuild the plugin"; goto fail; }
	const tibia_api *a = m->api;
	const tibia_info *info = &a->info;
	error = "invalid plugin API or metadata";
	if (!a->create || !a->destroy || !a->reset || !a->process || !info->json
		|| (info->count && !info->parameters) || (info->bus_count && !info->buses)) goto fail;
	for (size_t i = 0; i < info->count; ++i) {
		const tibia_parameter *p = info->parameters + i;
		if (!p->id || !*p->id || !p->name || !p->unit || !isfinite(p->minimum) || !isfinite(p->maximum)
			|| !isfinite(p->default_value) || p->minimum > p->default_value || p->default_value > p->maximum
			|| ((p->flags & TIBIA_PARAM_INTEGER) && floorf(p->default_value) != p->default_value)
			|| ((p->flags & TIBIA_PARAM_OUTPUT) ? !a->get_parameter : !a->set_parameter)) goto fail;
		for (size_t j = 0; j < i; ++j) if (!strcmp(p->id, info->parameters[j].id)) goto fail;
	}
	error = "host requires one mono/stereo output, at most one mono/stereo input and one MIDI input";
	for (size_t i = 0; i < info->bus_count; ++i) {
		const tibia_bus *b = info->buses + i;
		if (b->flags & TIBIA_BUS_MIDI) {
			if ((b->flags & TIBIA_BUS_OUTPUT) || m->midi >= 0 || !a->midi_msg_in) goto fail;
			m->midi = (int)i;
		} else {
			int *channels = b->flags & TIBIA_BUS_OUTPUT ? &m->output : &m->input;
			if (*channels || (b->channels != 1 && b->channels != 2)
				|| (b->flags & (TIBIA_BUS_CV | TIBIA_BUS_SIDECHAIN))) goto fail;
			*channels = (int)b->channels;
		}
	}
	if (!m->output) goto fail;
	return m;
fail:
	fprintf(stderr, "[Loader] %s: %s\n", path, error);
	tibia_loader_unload(m);
	return NULL;
}

void tibia_loader_unload(TibiaModule *m) {
	if (m) { if (m->handle) dlclose(m->handle); free(m); }
}
