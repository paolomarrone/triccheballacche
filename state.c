#include "state.h"
#include <stdlib.h>
#include <string.h>

void state_free(State *s) {
	free(s->data);
	*s = (State){0};
}
int state_write(State *s, const void *data, size_t size) {
	if ((size && !data) || s->failed || s->position > STATE_LIMIT || size > STATE_LIMIT - s->position)
		goto fail;
	size_t end = s->position + size;
	if (end > s->capacity) {
		size_t capacity = end > s->capacity * 2 ? end : s->capacity * 2;
		if (capacity > STATE_LIMIT)
			capacity = STATE_LIMIT;
		unsigned char *p = realloc(s->data, capacity);
		if (!p)
			goto fail;
		s->data = p;
		s->capacity = capacity;
	}
	if (size)
		memcpy(s->data + s->position, data, size);
	s->position = end;
	if (end > s->length)
		s->length = end;
	return 0;
fail:
	s->failed = 1;
	return -1;
}
size_t state_read(State *s, void *data, size_t size) {
	if (s->position > s->length)
		return 0;
	if (size > s->length - s->position)
		size = s->length - s->position;
	if (size)
		memcpy(data, s->data + s->position, size);
	s->position += size;
	return size;
}
int state_u32(State *s, uint32_t *value, int write) {
	unsigned char bytes[4];
	if (write) {
		for (int i = 0; i < 4; ++i)
			bytes[i] = *value >> (8 * i);
		return state_write(s, bytes, sizeof(bytes));
	}
	if (state_read(s, bytes, sizeof(bytes)) != sizeof(bytes))
		return -1;
	*value = 0;
	for (int i = 0; i < 4; ++i)
		*value |= (uint32_t)bytes[i] << (8 * i);
	return 0;
}
int state_string(State *s, const char **value, int write) {
	if (write)
		return state_write(s, *value, strlen(*value) + 1);
	if (s->position >= s->length)
		return -1;
	const char *p = (const char *)s->data + s->position;
	const char *end = memchr(p, 0, s->length - s->position);
	if (!end)
		return -1;
	*value = p;
	s->position += end - p + 1;
	return 0;
}
