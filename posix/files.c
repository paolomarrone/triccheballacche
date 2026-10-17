#include "host/platform.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int host_file_info(const char *path, HostFileInfo *out) {
	struct stat info;
	if (stat(path, &info))
		return -1;
	if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) {
		errno = EINVAL;
		return -1;
	}
	*out = (HostFileInfo){
	    info.st_size, (uint64_t)info.st_mtim.tv_sec * 1000000000 + info.st_mtim.tv_nsec, S_ISDIR(info.st_mode)};
	return 0;
}
FILE *host_fopen(const char *path, const char *mode) {
	return fopen(path, mode);
}
FILE *host_tempfile(char *path, const char *preserve_mode_from) {
	int fd = host_mkstemp(path);
	if (fd < 0)
		return NULL;
	struct stat info;
	int failed = preserve_mode_from && !stat(preserve_mode_from, &info) && fchmod(fd, info.st_mode & 0777);
	FILE *file = failed ? NULL : fdopen(fd, "w+b");
	if (!file) {
		close(fd);
		unlink(path);
	}
	return file;
}
int host_seek(FILE *file, int64_t offset, int origin) {
	return fseeko(file, offset, origin);
}
int host_remove(const char *path) {
	return unlink(path);
}
FILE *host_scan_output(void) {
	int fd = dup(STDOUT_FILENO);
	if (fd < 0)
		return NULL;
	FILE *file = dup2(STDERR_FILENO, STDOUT_FILENO) < 0 ? NULL : fdopen(fd, "w");
	if (!file)
		close(fd);
	return file;
}
int host_directory(const char *path, void (*visit)(void *, const char *, int), void *context) {
	DIR *dir = opendir(path);
	if (!dir)
		return -1;
	struct dirent *entry;
	while ((entry = readdir(dir))) {
		struct stat info;
		if (!fstatat(dirfd(dir), entry->d_name, &info, 0) && (S_ISDIR(info.st_mode) || S_ISREG(info.st_mode)))
			visit(context, entry->d_name, S_ISDIR(info.st_mode));
	}
	closedir(dir);
	return 0;
}
