#include "files.h"
#include "script.h"
#include "util.h"
#include "json_write.h"
#include "discovery.h"
#include "host/platform.h"
#include <errno.h>
#include <stdio.h>

const char *file_read(const char *path, char **text) {
	*text = NULL;
	HostFileInfo info;
	if (host_file_info(path, &info))
		return strerror(errno);
	if (info.directory)
		return "Path is not a regular file";
	if (info.size > MAX_SOURCE)
		return "Cannot read file (at most 8 MiB of text)";
	FILE *file = host_fopen(path, "rb");
	if (!file)
		return strerror(errno);
	size_t size = info.size;
	char *data = malloc(size + 1);
	const char *error = NULL;
	if (!data)
		error = "Out of memory";
	else if (fread(data, 1, size, file) != size || ferror(file) || memchr(data, 0, size))
		error = "Cannot read text file";
	if (error)
		free(data);
	else {
		data[size] = 0;
		*text = data;
	}
	fclose(file);
	return error;
}

const char *file_save(const char *path, const char *text) {
	// Follow an existing symlink and preserve permissions; publish only a complete write.
	char *resolved = host_path(path);
	const char *target = resolved ? resolved : path;
	char *temporary = malloc(strlen(target) + sizeof(".XXXXXX"));
	if (!temporary) {
		free(resolved);
		return "Out of memory";
	}
	sprintf(temporary, "%s.XXXXXX", target);
	int result = -1;
	FILE *file = host_tempfile(temporary, target);
	if (file) {
		result = fputs(text, file) < 0;
		result |= fclose(file) != 0;
		if (!result)
			result = host_replace(temporary, target);
		if (result)
			host_remove(temporary);
	}
	free(temporary);
	free(resolved);
	return result ? "Cannot save file; the previous file is preserved" : NULL;
}

static const char library_source[] =
#include "library.inc"
    ;

static Janet save_settings(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 1);
	const char *text = janet_getcstring(argv, 0);
	char *path = plugin_settings_path();
	if (!path)
		janet_panic("No user configuration directory");
	const char *error = NULL;
	for (char *p = path + 1; *p; ++p) {
		if (*p != '/')
			continue;
		*p = 0;
		int result = host_mkdir(path);
		*p = '/';
		if (result && errno != EEXIST) {
			error = strerror(errno);
			break;
		}
	}
	if (!error)
		error = file_save(path, text);
	free(path);
	if (error)
		janet_panic(error);
	return janet_wrap_nil();
}

char *library_json(const char *settings) {
	JanetTable *env = script_env(DEFAULT_SAMPLE_RATE);
	if (!env)
		return NULL;
	Janet result;
	char *json = NULL;
	janet_def(env, "host/save-settings", janet_wrap_cfunction(save_settings), NULL);
	janet_def(env, "host/settings-input", settings ? janet_cstringv(settings) : janet_wrap_nil(), NULL);
	if (!janet_dostring(env, library_source, "lib/library.janet", &result) && janet_checktype(result, JANET_STRING))
		json = copy_string((const char *)janet_unwrap_string(result));
	janet_deinit();
	return json;
}

typedef struct {
	Json *json;
	int count;
} Directory;
static void directory_entry(void *context, const char *name, int directory) {
	if (name[0] == '.')
		return;
	Directory *d = context;
	json_print(d->json, "%s{\"name\":", d->count++ ? "," : "");
	json_string(d->json, name);
	json_print(d->json, ",\"directory\":%s}", directory ? "true" : "false");
}
char *directory_json(const char *path) {
	char *resolved = host_path(*path ? path : ".");
	Json json = {0};
	json_print(&json, "{\"path\":");
	json_string(&json, resolved);
	json_print(&json, ",\"files\":[");
	Directory directory = {&json, 0};
	const char *error = !resolved || host_directory(resolved, directory_entry, &directory) ? strerror(errno) : NULL;
	json_print(&json, "],\"error\":");
	json_string(&json, error);
	json_print(&json, "}");
	free(resolved);
	if (json.failed) {
		free(json.data);
		return NULL;
	}
	return json.data;
}
