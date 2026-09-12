#ifndef JSON_WRITE_H
#define JSON_WRITE_H
#include <stddef.h>

typedef struct {
	char *data;
	size_t length, capacity;
	int failed;
} Json;

// Zero-initialize, append, check failed, then free data. Strings are escaped; printf is for JSON syntax/numbers.
void json_print(Json *json, const char *format, ...);
void json_string(Json *json, const char *value);
#endif
