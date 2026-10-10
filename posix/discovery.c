#include "discovery.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static char *scan(const char *path) {
	int descriptors[2];
	if (pipe(descriptors))
		return NULL;
	fcntl(descriptors[0], F_SETFD, FD_CLOEXEC);
	fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
	posix_spawn_file_actions_addclose(&actions, descriptors[0]);
	posix_spawn_file_actions_addclose(&actions, descriptors[1]);
	char *const argv[] = {"build/tools/plugin-scan", (char *)path, NULL};
	pid_t child;
	int result = posix_spawn(&child, argv[0], &actions, NULL, argv, environ);
	posix_spawn_file_actions_destroy(&actions);
	close(descriptors[1]);
	if (result) {
		close(descriptors[0]);
		fprintf(stderr, "[Scan] %s: %s\n", argv[0], strerror(result));
		return NULL;
	}
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	char *data = NULL;
	size_t used = 0, capacity = 0;
	int failed = 0, eof = 0, exited = 0, status = 0;
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		int remaining = 10000 - (now.tv_sec - start.tv_sec) * 1000 - (now.tv_nsec - start.tv_nsec) / 1000000;
		if (remaining <= 0) {
			failed = 1;
			break;
		}
		pid_t waited = waitpid(child, &status, WNOHANG);
		if (waited == child)
			exited = 1;
		else if (waited < 0 && errno != EINTR && !exited) {
			failed = 1;
			break;
		}
		if (eof && exited)
			break;
		struct pollfd fd = {.fd = descriptors[0], .events = POLLIN};
		int ready = poll(&fd, eof ? 0 : 1, remaining < 20 ? remaining : 20);
		if (ready < 0 && errno == EINTR)
			continue;
		if (ready < 0) {
			failed = 1;
			break;
		}
		if (!ready || eof)
			continue;
		if (used + 4096 + 1 > capacity) {
			size_t next = capacity ? 2 * capacity : 8192;
			if (next > 32 * 1024 * 1024) {
				failed = 1;
				break;
			}
			char *p = realloc(data, next);
			if (!p) {
				failed = 1;
				break;
			}
			data = p;
			capacity = next;
		}
		ssize_t size = read(descriptors[0], data + used, 4096);
		if (size < 0 && errno == EINTR)
			continue;
		if (size < 0) {
			failed = 1;
			break;
		}
		if (!size) {
			eof = 1;
			continue;
		}
		used += size;
	}
	close(descriptors[0]);
	if (!exited) {
		kill(child, SIGKILL);
		while (waitpid(child, &status, 0) < 0)
			if (errno != EINTR) {
				failed = 1;
				break;
			}
	}
	if (failed || !WIFEXITED(status) || WEXITSTATUS(status) || !used) {
		fprintf(stderr, "[Scan] %s: failed, crashed or timed out\n", path);
		free(data);
		return NULL;
	}
	data[used] = 0;
	return data;
}

// Preparation and catalog requests use independent Janet VMs. Cache scans outside
// those VMs, keyed by the resolved file and its stamp; instances have a separate cache.
typedef struct Scan {
	struct Scan *next;
	char *path, *json;
	struct timespec modified;
	off_t size;
} Scan;
static Scan *cache;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t cleanup_once = PTHREAD_ONCE_INIT;
static void cleanup(void) {
	while (cache) {
		Scan *next = cache->next;
		free(cache->path);
		free(cache->json);
		free(cache);
		cache = next;
	}
}
static void register_cleanup(void) {
	atexit(cleanup);
}
char *scan_plugins(const char *path) {
	struct stat info;
	char *resolved = realpath(path, NULL);
	if (!resolved || stat(resolved, &info) || (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode))) {
		free(resolved);
		return NULL;
	}
	// RDF bundles may refer to several changing files. Read their metadata afresh.
	if (S_ISDIR(info.st_mode)) {
		char *result = scan(resolved);
		free(resolved);
		return result;
	}
	pthread_once(&cleanup_once, register_cleanup);
	pthread_mutex_lock(&mutex);
	Scan *entry = cache;
	for (; entry && strcmp(entry->path, resolved); entry = entry->next) {
	}
	if (!entry) {
		entry = calloc(1, sizeof(*entry));
		if (!entry) {
			pthread_mutex_unlock(&mutex);
			free(resolved);
			return NULL;
		}
		entry->path = resolved;
		resolved = NULL;
		entry->next = cache;
		cache = entry;
	}
	if (!entry->json || entry->size != info.st_size || entry->modified.tv_sec != info.st_mtim.tv_sec ||
	    entry->modified.tv_nsec != info.st_mtim.tv_nsec) {
		free(entry->json);
		entry->json = scan(entry->path);
		entry->modified = info.st_mtim;
		entry->size = info.st_size;
	}
	char *result = entry->json ? copy_string(entry->json) : NULL;
	pthread_mutex_unlock(&mutex);
	free(resolved);
	return result;
}
