#include "discovery.h"
#include "util.h"
#include "host/platform.h"
#include "thread.h"

// Preparation and catalog requests use independent Janet VMs. Cache scans outside
// those VMs, keyed by the resolved file and its stamp; instances have a separate cache.
typedef struct Scan {
	struct Scan *next;
	char *path, *json;
	uint64_t modified, size;
} Scan;
static Scan *cache;
static Mutex mutex = MUTEX_INITIALIZER;
static int cleanup_registered;
static void cleanup(void) {
	while (cache) {
		Scan *next = cache->next;
		free(cache->path);
		free(cache->json);
		free(cache);
		cache = next;
	}
}
void scan_plugins_clear(void) {
	mutex_lock(&mutex);
	cleanup();
	mutex_unlock(&mutex);
}
char *scan_plugins(const char *path) {
	HostFileInfo info;
	char *resolved = host_path(path);
	if (!resolved || host_file_info(resolved, &info)) {
		free(resolved);
		return NULL;
	}
	// Bundles contain multiple metadata/resources/binaries. Read their metadata afresh.
	if (info.directory) {
		char *result = host_scan(resolved);
		free(resolved);
		return result;
	}
	mutex_lock(&mutex);
	if (!cleanup_registered) {
		atexit(cleanup);
		cleanup_registered = 1;
	}
	Scan *entry = cache;
	for (; entry && strcmp(entry->path, resolved); entry = entry->next) {
	}
	if (!entry) {
		entry = calloc(1, sizeof(*entry));
		if (!entry) {
			mutex_unlock(&mutex);
			free(resolved);
			return NULL;
		}
		entry->path = resolved;
		resolved = NULL;
		entry->next = cache;
		cache = entry;
	}
	if (!entry->json || entry->size != info.size || entry->modified != info.modified) {
		free(entry->json);
		entry->json = host_scan(entry->path);
		entry->modified = info.modified;
		entry->size = info.size;
	}
	char *result = entry->json ? copy_string(entry->json) : NULL;
	mutex_unlock(&mutex);
	free(resolved);
	return result;
}
