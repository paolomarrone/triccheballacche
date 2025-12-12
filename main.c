#include <stdio.h>
#include "loader.h"

int main() {

	const char *plugin_path = "examples/tibia_test/plugin.so";

	printf("Loading plugin from: %s\n", plugin_path);

	TibiaModule *mod = tibia_loader_load(plugin_path);

	if (!mod) {
		fprintf(stderr, "FAILED to load plugin.\n");
		return 1;
	}

	printf("OK: Plugin loaded.\n");

	printf("process address: %p\n", (void*)mod->process);

	tibia_loader_unload(mod);
	printf("Plugin unloaded.\n");

	return 0;
}