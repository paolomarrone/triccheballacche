#include "json_write.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void json_print(Json *text, const char *format, ...) {
	if (text->failed)
		return;
	va_list args, copy;
	va_start(args, format);
	va_copy(copy, args);
	int n = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (n < 0 || (size_t)n > SIZE_MAX - text->length - 1) {
		text->failed = 1;
	} else if (text->length + n + 1 > text->capacity) {
		size_t capacity = text->length + n + 1;
		if (capacity < text->capacity * 2)
			capacity = text->capacity * 2;
		if (capacity < 1024)
			capacity = 1024;
		char *data = realloc(text->data, capacity);
		if (!data)
			text->failed = 1;
		else {
			text->data = data;
			text->capacity = capacity;
		}
	}
	if (!text->failed) {
		vsnprintf(text->data + text->length, text->capacity - text->length, format, args);
		text->length += n;
	}
	va_end(args);
}

void json_string(Json *text, const char *value) {
	json_print(text, "\"");
	const char *p = value ? value : "";
	while (*p) {
		const char *start = p;
		while ((unsigned char)*p >= 32 && *p != '"' && *p != '\\')
			++p;
		if (p != start)
			json_print(text, "%.*s", (int)(p - start), start);
		if (*p) {
			if ((unsigned char)*p < 32)
				json_print(text, "\\u%04x", (unsigned char)*p);
			else
				json_print(text, "\\%c", *p);
			++p;
		}
	}
	json_print(text, "\"");
}
