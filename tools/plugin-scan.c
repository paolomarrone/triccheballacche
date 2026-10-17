#include "host/clap.h"
#include "host/lv2.h"
#include "host/vst3.h"
#include "host/platform.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	if (argc != 2) {
		fputs("Usage: plugin-scan module.clap|bundle.lv2|bundle.vst3\n", stderr);
		return 1;
	}
	// Plugin diagnostics must not corrupt the scanner's JSON stream.
	FILE *output = host_scan_output();
	if (!output)
		return 1;
	char *path = host_path(argv[1]);
	HostFileInfo info;
	Json json = {0};
	int result = 1;
	if (!path || host_file_info(path, &info))
		goto done;
	size_t length = strlen(path);
	int vst3 = length > 5 && !strcmp(path + length - 5, ".vst3");
	if (vst3 ? vst_scan(&json, path) : info.directory ? lv2_scan(&json, path) : clap_scan(&json, path))
		goto done;
	result = fputs(json.data, output) < 0;
done:
	result |= fclose(output) != 0;
	free(path);
	free(json.data);
	return result;
}
