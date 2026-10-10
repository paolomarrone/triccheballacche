#include "posix/clap.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
	if (argc != 2) {
		fputs("Usage: plugin-scan module.clap\n", stderr);
		return 1;
	}
	// Plugin diagnostics must not corrupt the scanner's JSON stream.
	int output = dup(STDOUT_FILENO);
	if (output < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
		return 1;
	Module *m = module_open(NULL, argv[1], PLUGIN_CLAP);
	if (!m)
		return 1;
	const clap_plugin_entry_t *entry = module_symbol(m, "clap_entry");
	const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	int result = 1;
	Json json = {0};
	if (!factory || !factory->get_plugin_count || !factory->get_plugin_descriptor || !factory->create_plugin)
		goto done;
	uint32_t count = factory->get_plugin_count(factory);
	if (count > 4096)
		goto done;
	json_print(&json, "[");
	unsigned found = 0;
	for (uint32_t i = 0; i < count; ++i) {
		const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, i);
		if (!desc || !desc->id || !desc->name || !clap_version_is_compatible(desc->clap_version))
			goto done;
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
		json_print(&json, "%s%s", found++ ? "," : "", description.data);
		free(description.data);
	}
	json_print(&json, "]");
	if (!json.failed) {
		FILE *file = fdopen(output, "w");
		if (file) {
			result = fputs(json.data, file) < 0;
			result |= fclose(file) != 0;
			output = -1;
		}
	}
done:
	if (output >= 0)
		close(output);
	free(json.data);
	module_close(m);
	return result;
}
