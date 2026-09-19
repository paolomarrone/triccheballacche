#include "files.h"
#include "script.h"
#include "util.h"
#include "json_write.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

const char *file_read(const char *path, char **text) {
	struct stat info;
	if (stat(path, &info))
		return strerror(errno);
	if (!S_ISREG(info.st_mode))
		return "Path is not a regular file";
	FILE *file = fopen(path, "rb");
	if (!file)
		return strerror(errno);
	long size = -1;
	if (!fseek(file, 0, SEEK_END))
		size = ftell(file);
	const char *error = "Cannot read file (at most 8 MiB of text)";
	if (size >= 0 && size <= MAX_SOURCE && !fseek(file, 0, SEEK_SET)) {
		*text = malloc((size_t)size + 1);
		if (*text && fread(*text, 1, size, file) == (size_t)size && !ferror(file) && !memchr(*text, 0, size)) {
			(*text)[size] = 0;
			error = NULL;
		}
	}
	fclose(file);
	if (error) {
		free(*text);
		*text = NULL;
	}
	return error;
}

const char *file_save(const char *path, const char *text) {
	// Follow an existing symlink and preserve permissions; publish only a complete write.
	char *resolved = realpath(path, NULL);
	const char *target = resolved ? resolved : path;
	char *temporary = malloc(strlen(target) + sizeof(".XXXXXX"));
	if (!temporary) {
		free(resolved);
		return "Out of memory";
	}
	sprintf(temporary, "%s.XXXXXX", target);
	struct stat info;
	int exists = !stat(target, &info), fd = mkstemp(temporary), result = -1;
	if (fd >= 0) {
		FILE *file = fdopen(fd, "wb");
		if (file) {
			result = (exists && fchmod(fd, info.st_mode & 0777)) || fputs(text, file) < 0;
			result |= fclose(file) != 0;
		} else {
			close(fd);
		}
		if (!result)
			result = rename(temporary, target);
		if (result)
			unlink(temporary);
	}
	free(temporary);
	free(resolved);
	return result ? "Cannot save file; the previous file is preserved" : NULL;
}

static const char library_source[] =
#include "build/generated/library.inc"
    ;

char *library_json(void) {
	JanetTable *env = script_env();
	if (!env)
		return NULL;
	Janet result;
	char *json = NULL;
	if (!janet_dostring(env, library_source, "lib/library.janet", &result) && janet_checktype(result, JANET_STRING))
		json = copy_string((const char *)janet_unwrap_string(result));
	janet_deinit();
	return json;
}

char *directory_json(const char *path) {
	char *resolved = realpath(*path ? path : ".", NULL);
	DIR *dir = resolved ? opendir(resolved) : NULL;
	const char *error = dir ? NULL : strerror(errno);
	Json json = {0};
	json_print(&json, "{\"path\":");
	json_string(&json, resolved);
	json_print(&json, ",\"files\":[");
	if (dir) {
		struct dirent *entry;
		int count = 0;
		while ((entry = readdir(dir))) {
			if (entry->d_name[0] == '.')
				continue;
			struct stat info;
			if (fstatat(dirfd(dir), entry->d_name, &info, 0) || (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)))
				continue;
			json_print(&json, "%s{\"name\":", count++ ? "," : "");
			json_string(&json, entry->d_name);
			json_print(&json, ",\"directory\":%s}", S_ISDIR(info.st_mode) ? "true" : "false");
		}
		closedir(dir);
	}
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
