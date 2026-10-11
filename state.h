#ifndef STATE_H
#define STATE_H
#include <stddef.h>
#include <stdint.h>

enum { STATE_LIMIT = 64 * 1024 * 1024, STATE_UNSUPPORTED = 1 };
// Owned, bounded bytes. State operations run with audio stopped.
typedef struct {
	unsigned char *data;
	size_t length, capacity, position;
	int failed;
} State;
void state_free(State *state);
int state_write(State *state, const void *data, size_t size);
size_t state_read(State *state, void *data, size_t size);
int state_u32(State *state, uint32_t *value, int write);
// Read strings borrow the buffer; stored strings include their terminator.
int state_string(State *state, const char **value, int write);
#endif
