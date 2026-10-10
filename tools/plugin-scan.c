#include "host/clap.h"
#include "host/lv2.h"
#include "host/vst3.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int clap_scan(Json *json, const char *path) {
	Module *m = module_open(NULL, path, PLUGIN_CLAP);
	if (!m)
		return -1;
	const clap_plugin_entry_t *entry = clap_module_entry(m);
	const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	if (!factory || !factory->get_plugin_count || !factory->get_plugin_descriptor || !factory->create_plugin)
		goto fail;
	uint32_t count = factory->get_plugin_count(factory);
	if (count > 4096)
		goto fail;
	json_print(json, "[");
	unsigned found = 0;
	for (uint32_t i = 0; i < count; ++i) {
		const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, i);
		if (!desc || !desc->id || !desc->name || !clap_version_is_compatible(desc->clap_version))
			goto fail;
		Json description = {0};
		Clap c = {0};
		int created = clap_create(&c, m, desc->id);
		if (created || clap_metadata(&c, &description, module_path(m))) {
			fprintf(stderr, "[Scan] %s: %s (audio buses %u/%u, note ports %u/%u)\n", desc->id,
			    created ? "initialization/parameters failed" : "unsupported layout",
			    c.audio ? c.audio->count(c.plugin, true) : 0, c.audio ? c.audio->count(c.plugin, false) : 0,
			    c.notes ? c.notes->count(c.plugin, true) : 0, c.notes ? c.notes->count(c.plugin, false) : 0);
			clap_destroy(&c);
			free(description.data);
			continue;
		}
		clap_destroy(&c);
		json_print(json, "%s%s", found++ ? "," : "", description.data);
		free(description.data);
	}
	json_print(json, "]");
	module_close(m);
	return json->failed ? -1 : 0;
fail:
	module_close(m);
	return -1;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fputs("Usage: plugin-scan module.clap|bundle.lv2|bundle.vst3\n", stderr);
		return 1;
	}
	// Plugin diagnostics must not corrupt the scanner's JSON stream.
	int output = dup(STDOUT_FILENO);
	if (output < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
		return 1;
	char *path = realpath(argv[1], NULL);
	struct stat info;
	Json json = {0};
	int result = 1;
	if (!path || stat(path, &info))
		goto done;
	size_t length = strlen(path);
	int vst3 = length > 5 && !strcmp(path + length - 5, ".vst3");
	if (vst3 ? vst_scan(&json, path) : S_ISDIR(info.st_mode) ? lv2_scan(&json, path) : clap_scan(&json, path))
		goto done;
	FILE *file = fdopen(output, "w");
	if (file) {
		result = fputs(json.data, file) < 0;
		result |= fclose(file) != 0;
		output = -1;
	}
done:
	if (output >= 0)
		close(output);
	free(path);
	free(json.data);
	return result;
}
