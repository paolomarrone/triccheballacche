#include "discovery.h"
#include <stdio.h>
#include <stddef.h>
char *scan_plugins(const char *path) {
	(void)path;
	fputs("Native plugins require the native host; use Perone/Wasm in the browser\n", stderr);
	return NULL;
}
