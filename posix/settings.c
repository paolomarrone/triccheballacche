#include "discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *plugin_settings_path(void) {
	const char *root = getenv("XDG_CONFIG_HOME"), *suffix = "/triccheballacche/settings.json";
	if (!root || root[0] != '/') {
		root = getenv("HOME");
		suffix = "/.config/triccheballacche/settings.json";
	}
	if (!root || !*root)
		return NULL;
	char *path = malloc(strlen(root) + strlen(suffix) + 1);
	if (path)
		sprintf(path, "%s%s", root, suffix);
	return path;
}
