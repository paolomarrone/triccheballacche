#include "discovery.h"
#include <stdio.h>
#include <stddef.h>
#include <emscripten.h>
// clang-format off
EM_JS_DEPS(plugin_paths, "$stringToNewUTF8");
EM_JS(char *, plugin_default_paths, (void), {
	return Module.pluginPaths ? stringToNewUTF8(JSON.stringify(Module.pluginPaths)) : 0;
});
// clang-format on
char *plugin_settings_path(void) {
	return NULL;
}
void scan_plugins_clear(void) {
}
char *scan_plugins(const char *path) {
	(void)path;
	fputs("Native plugins require the native host; use Perone/Wasm in the browser\n", stderr);
	return NULL;
}
