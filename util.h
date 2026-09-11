#ifndef UTIL_H
#define UTIL_H
#include <stdlib.h>
#include <string.h>

static inline char *copy_string(const char *s) {
	size_t size = strlen(s) + 1;
	char *copy = malloc(size);
	return copy ? memcpy(copy, s, size) : NULL;
}
#endif
