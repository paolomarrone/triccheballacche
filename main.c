#include <stdio.h>
#include "loader.h"

int main() {

	const char *tibia_test_path = "examples/tibia_test/plugin.so";
	const char *synth_mono_path = "examples/synth_mono/plugin.so";

	TibiaModule *synth_mono = tibia_loader_load(synth_mono_path);
	TibiaModule *tibia_test = tibia_loader_load(tibia_test_path);

	if (!synth_mono || !tibia_test) {
		fprintf(stderr, "FAILED to load plugin.\n");
		return 1;
	}

	printf("OK: Plugins loaded.\n");

	printf("synth_mono process address: %p\n", (void*)synth_mono->process);
	printf("tibia_test process address: %p\n", (void*)tibia_test->process);

	tibia_loader_unload(tibia_test);
	tibia_loader_unload(synth_mono);

	printf("Plugins unloaded.\n");

	return 0;
}