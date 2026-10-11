#include "host/clap.h"
#include "host/lv2.h"
#include "host/vst3.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
