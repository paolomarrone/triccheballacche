#include "module.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Module {
	Module *next;
	const ModuleOps *ops;
	void *data;
	char *path;
	size_t references;
};

void module_close(Module *m) {
	if (!m || --m->references)
		return;
	if (m->data)
		m->ops->close(m->data);
	free(m->path);
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
	const ModuleOps *const formats[] = {&perone_module_ops, &clap_module_ops, &lv2_module_ops};
	if (format < 0 || format >= PLUGIN_FORMATS)
		return NULL;
	const ModuleOps *ops = formats[format];
	char *resolved = host_path(path);
	if (!resolved) {
		fprintf(stderr, "[Loader] %s: cannot resolve plugin path\n", path);
		return NULL;
	}
	for (Module *m = modules ? modules->head : NULL; m; m = m->next)
		if (m->ops == ops && !strcmp(m->path, resolved)) {
			free(resolved);
			++m->references;
			return m;
		}
	Module *m = calloc(1, sizeof(*m));
	if (!m) {
		free(resolved);
		return NULL;
	}
	m->ops = ops;
	m->path = resolved;
	m->references = 1;
	m->data = ops->open(resolved);
	if (!m->data) {
		fprintf(stderr, "[Loader] %s: cannot initialize module\n", path);
		module_close(m);
		return NULL;
	}
	if (modules) {
		++m->references;
		m->next = modules->head;
		modules->head = m;
	}
	return m;
}

void *module_data(Module *m) {
	return m->data;
}
const char *module_path(Module *m) {
	return m->path;
}
