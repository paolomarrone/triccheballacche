#include "midi.h"
#include <string.h>

size_t midi_size(uint8_t status) {
	if (status < 0x80)
		return 0;
	if (status < 0xf0)
		return (status & 0xe0) == 0xc0 ? 2 : 3;
	switch (status) {
	case 0xf1:
	case 0xf3:
		return 2;
	case 0xf2:
		return 3;
	case 0xf6:
	case 0xf8:
	case 0xfa:
	case 0xfb:
	case 0xfc:
	case 0xfe:
	case 0xff:
		return 1;
	default:
		return 0;
	}
}
int midi_valid(const uint8_t *data, size_t size) {
	if (!data || !size || size > MIDI_MESSAGE)
		return 0;
	if (data[0] == 0xf0) {
		if (size < 2 || data[size - 1] != 0xf7)
			return 0;
		--size;
	} else if (midi_size(data[0]) != size)
		return 0;
	for (size_t i = 1; i < size; ++i)
		if (data[i] & 0x80)
			return 0;
	return 1;
}
void midi_clear(MidiBuffer *b) {
	b->count = b->used = b->failed = 0;
}
int midi_add(MidiBuffer *b, uint32_t frame, const uint8_t *data, size_t size) {
	if (b->failed || !midi_valid(data, size) || b->count == MIDI_EVENTS || size > MIDI_BYTES - b->used) {
		b->failed = 1;
		return -1;
	}
	unsigned i = b->count++;
	while (i && b->events[i - 1].frame > frame) {
		b->events[i] = b->events[i - 1];
		--i;
	}
	b->events[i] = (MidiEvent){frame, b->used, size};
	memcpy(b->data + b->used, data, size);
	b->used += size;
	return 0;
}
int midi_merge(MidiBuffer *to, const MidiBuffer *from, uint32_t offset) {
	if (from->failed)
		return to->failed = 1, -1;
	for (unsigned i = 0; i < from->count; ++i) {
		const MidiEvent *e = from->events + i;
		if (e->frame > UINT32_MAX - offset || midi_add(to, e->frame + offset, from->data + e->offset, e->size))
			return to->failed = 1, -1;
	}
	return 0;
}
int midi_push(MidiQueue *q, const MidiPacket *p) {
	unsigned w = atomic_load_explicit(&q->write, memory_order_relaxed);
	if (!midi_valid(p->data, p->size) || w - atomic_load_explicit(&q->read, memory_order_acquire) == MIDI_EVENTS) {
		atomic_store(&q->failed, 1);
		return -1;
	}
	q->packets[w % MIDI_EVENTS] = *p;
	atomic_store_explicit(&q->write, w + 1, memory_order_release);
	return 0;
}
int midi_pop(MidiQueue *q, MidiPacket *p) {
	unsigned r = atomic_load_explicit(&q->read, memory_order_relaxed);
	if (r == atomic_load_explicit(&q->write, memory_order_acquire))
		return 0;
	*p = q->packets[r % MIDI_EVENTS];
	atomic_store_explicit(&q->read, r + 1, memory_order_release);
	return 1;
}
