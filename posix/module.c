#include "module.h"
#include "util.h"
#include <clap/entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

struct Module {
	Module *next;
	void *handle;
	const clap_plugin_entry_t *entry;
	int format, initialized;
	char *path, *bindir, *datadir;
	size_t references;
};

void module_close(Module *m) {
	if (!m || --m->references)
		return;
	if (m->initialized)
		m->entry->deinit();
	if (m->handle)
		dlclose(m->handle);
	free(m->path);
	free(m->bindir);
	free(m->datadir);
	free(m);
}

void modules_free(Modules *modules) {
	while (modules->head) {
		Module *m = modules->head;
		modules->head = m->next;
		module_close(m);
	}
}

Module *module_open(Modules *modules, const char *path, int format) {
	char *resolved = realpath(path, NULL);
	if (!resolved) {
		fprintf(stderr, "[Loader] %s: cannot resolve plugin binary\n", path);
		return NULL;
	}
	for (Module *m = modules ? modules->head : NULL; m; m = m->next)
		if (m->format == format && !strcmp(m->path, resolved)) {
			free(resolved);
			++m->references;
			return m;
		}
	Module *m = calloc(1, sizeof(*m));
	if (!m) {
		free(resolved);
		return NULL;
	}
	m->references = 1;
	m->path = resolved;
	m->format = format;
	const char *error = "out of memory";
	m->bindir = copy_string(resolved);
	if (!m->bindir)
		goto fail;
	*strrchr(m->bindir, '/') = 0;
	m->datadir = copy_string(m->bindir);
	if (!m->datadir)
		goto fail;
	char *slash = strrchr(m->datadir, '/');
	if (!slash) {
		error = "plugin binary has no bundle directory";
		goto fail;
	}
	*slash = 0;
	m->handle = dlopen(resolved, RTLD_NOW | RTLD_LOCAL);
	if (!m->handle) {
		error = dlerror();
		goto fail;
	}
	if (format == PLUGIN_CLAP) {
		m->entry = dlsym(m->handle, "clap_entry");
		error = "unsupported CLAP entry";
		if (!m->entry || !clap_version_is_compatible(m->entry->clap_version) || !m->entry->init || !m->entry->deinit ||
		    !m->entry->get_factory || !m->entry->init(resolved))
			goto fail;
		m->initialized = 1;
	}

	if (modules) {
		++m->references;
		m->next = modules->head;
		modules->head = m;
	}
	return m;
fail:
	fprintf(stderr, "[Loader] %s: %s\n", path, error);
	module_close(m);
	return NULL;
}

void *module_symbol(Module *m, const char *symbol) {
	return dlsym(m->handle, symbol);
}
const char *module_path(Module *m) {
	return m->path;
}
const char *module_bindir(Module *m) {
	return m->bindir;
}
const char *module_datadir(Module *m) {
	return m->datadir;
}
