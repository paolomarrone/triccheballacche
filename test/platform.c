#include "platform.h"
#include "host/platform.h"
#include "native/thread.h"
#include "native/files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	Mutex mutex;
	Condition changed;
	Semaphore ready;
	unsigned count;
} Worker;
static void *work(void *context) {
	Worker *w = context;
	for (unsigned i = 0; i < 3; ++i) {
		semaphore_wait(&w->ready);
		mutex_lock(&w->mutex);
		++w->count;
		condition_signal(&w->changed);
		mutex_unlock(&w->mutex);
	}
	return NULL;
}
static void directory_entry(void *context, const char *name, int directory) {
	if (!strcmp(name, "caffè-音楽.janet")) {
		assert(!directory);
		++*(unsigned *)context;
	}
}
int main(void) {
	Worker w = {0};
	assert(!mutex_init(&w.mutex) && !condition_init(&w.changed) && !semaphore_init(&w.ready));
	mutex_lock(&w.mutex);
	assert(condition_wait_ms(&w.changed, &w.mutex, 10) == 1);
	mutex_unlock(&w.mutex);
	Thread thread;
	assert(!thread_start(&thread, work, &w));
	for (unsigned i = 1; i <= 3; ++i) {
		semaphore_post(&w.ready);
		mutex_lock(&w.mutex);
		while (w.count != i)
			assert(condition_wait_ms(&w.changed, &w.mutex, 5000) == 0);
		mutex_unlock(&w.mutex);
	}
	thread_join(thread);
	semaphore_destroy(&w.ready);
	condition_destroy(&w.changed);
	mutex_destroy(&w.mutex);
	puts("OK: native threads, mutexes, condition notifications/timeouts and semaphores");

	char directory[] = "build/test/platform-XXXXXX", path[256], temporary[272];
	assert(mkdtemp(directory));
	snprintf(path, sizeof(path), "%s/caffè-音楽.janet", directory);
	for (int i = 0; i < 2; ++i) {
		snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
		FILE *file = host_tempfile(temporary, path);
		assert(file && fputs(i ? "replacement" : "first", file) >= 0 && !fclose(file));
		assert(!host_replace(temporary, path));
	}
	HostFileInfo info;
	assert(!host_file_info(path, &info) && !info.directory && info.size == 11);
	FILE *file = host_fopen(path, "rb");
	char content[12] = {0};
	assert(file && fread(content, 1, 11, file) == 11 && !strcmp(content, "replacement"));
	assert(!host_seek(file, -1, SEEK_END) && fgetc(file) == 't' && !fclose(file));
	unsigned found = 0;
	assert(!host_directory(directory, directory_entry, &found) && found == 1);
	char *text = NULL;
	assert(!file_read(path, &text) && !strcmp(text, "replacement"));
	free(text);
	assert(file_read(directory, &text) && !text);
	file = host_fopen(path, "wb");
	assert(file && !fclose(file));
	assert(!file_read(path, &text) && !*text);
	free(text);
	file = host_fopen(path, "wb");
	assert(file && fputc(0, file) == 0 && !fclose(file));
	assert(file_read(path, &text) && !text);
	file = host_fopen(path, "wb");
	assert(file && !host_seek(file, MAX_SOURCE, SEEK_SET) && fputc('x', file) == 'x' && !fclose(file));
	assert(file_read(path, &text) && !text);
	assert(!host_remove(path) && !rmdir(directory));
	assert(file_read(path, &text) && !text);
	puts("OK: UTF-8 file paths, temporary files, atomic replacement, metadata and directory listing");
	puts("OK: text reads, empty files, directories, embedded NULs, oversized and missing files");
}
