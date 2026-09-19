#ifndef FILES_H
#define FILES_H

enum { MAX_SOURCE = 8 * 1024 * 1024 };

const char *file_read(const char *path, char **text);
const char *file_save(const char *path, const char *text);
// Owned JSON. Library discovery starts and closes Janet without creating DSPs.
char *library_json(void);
char *directory_json(const char *path);
#endif
