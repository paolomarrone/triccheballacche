#ifndef MIDI_H
#define MIDI_H
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

enum { MIDI_EVENTS = 256, MIDI_BYTES = 16384, MIDI_MESSAGE = 1024 };
typedef struct {
	uint32_t frame, offset, size;
} MidiEvent;
// Owned bytes, stable time order. Prepared outside the audio callback.
typedef struct {
	MidiEvent events[MIDI_EVENTS];
	uint8_t data[MIDI_BYTES];
	unsigned count, used;
	int failed;
} MidiBuffer;
size_t midi_size(uint8_t status); // Zero for SysEx or an invalid status.
int midi_valid(const uint8_t *data, size_t size);
void midi_clear(MidiBuffer *buffer);
int midi_add(MidiBuffer *buffer, uint32_t frame, const uint8_t *data, size_t size);
int midi_merge(MidiBuffer *to, const MidiBuffer *from, uint32_t offset);

typedef struct {
	uint64_t frame;
	int node;
	unsigned size;
	uint8_t data[MIDI_MESSAGE];
} MidiPacket;
// One producer and one consumer. Overflow is an error, never a lost note-off.
typedef struct {
	atomic_uint read, write;
	atomic_int failed;
	MidiPacket packets[MIDI_EVENTS];
} MidiQueue;
int midi_push(MidiQueue *queue, const MidiPacket *packet);
int midi_pop(MidiQueue *queue, MidiPacket *packet);
#endif
