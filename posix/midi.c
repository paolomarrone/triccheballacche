#include "midi.h"
#include "json_write.h"
#include <portmidi.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
	PortMidiStream *stream;
	int node, input;
	unsigned used;
	uint8_t sysex[MIDI_MESSAGE];
} Port;
struct MidiDevices {
	pthread_t thread;
	pthread_mutex_t lock;
	int closing, count;
	Port *ports;
	Player *player;
	MidiPacket pending;
	int queued;
	PmTimestamp due;
	const char *error;
};
static PmTimestamp clock_ms(void *unused) {
	(void)unused;
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void close_port(Port *p) {
	if (!p->stream)
		return;
	// The host owns future events; the device only receives messages due now.
	if (!p->input)
		for (int channel = 0; channel < 16; ++channel) {
			Pm_WriteShort(p->stream, 0, Pm_Message(0xb0 | channel, 64, 0));
			Pm_WriteShort(p->stream, 0, Pm_Message(0xb0 | channel, 120, 0));
			Pm_WriteShort(p->stream, 0, Pm_Message(0xb0 | channel, 123, 0));
		}
	Pm_Close(p->stream);
	p->stream = NULL;
	p->used = 0;
}
static int receive(MidiDevices *d, Port *p, uint32_t word) {
	uint8_t status = word;
	if (status >= 0xf8)
		return midi_size(status) ? session_midi(d->player->session, p->node, &status, 1) : 0;
	if ((status & 0x80) && status != 0xf7)
		p->used = 0; // A new status abandons truncated SysEx.
	if (!p->used && status != 0xf0) {
		uint8_t data[] = {word, word >> 8, word >> 16};
		size_t size = midi_size(data[0]);
		return size ? session_midi(d->player->session, p->node, data, size) : 0;
	}
	for (int i = 0; i < 4; ++i, word >>= 8) {
		uint8_t byte = word;
		if (byte >= 0xf8) {
			if (midi_size(byte) && session_midi(d->player->session, p->node, &byte, 1))
				return -1;
			continue;
		}
		if (p->used == MIDI_MESSAGE)
			return -1;
		p->sysex[p->used++] = byte;
		if (byte == 0xf7) {
			int result = session_midi(d->player->session, p->node, p->sysex, p->used);
			p->used = 0;
			return result;
		}
	}
	return 0;
}
static void *poll_midi(void *data) {
	MidiDevices *d = data;
	for (;;) {
		pthread_mutex_lock(&d->lock);
		if (d->closing) {
			pthread_mutex_unlock(&d->lock);
			return NULL;
		}
		if (d->player && !d->error) {
			for (int i = 0; i < d->count; ++i) {
				Port *p = d->ports + i;
				if (!p->stream || !p->input)
					continue;
				PmEvent events[64];
				int n = Pm_Read(p->stream, events, 64);
				if (n < 0)
					d->error = Pm_GetErrorText(n);
				for (int j = 0; j < n; ++j)
					if (receive(d, p, events[j].message))
						d->error = "MIDI input overflow or invalid message";
			}
			Session *s = d->player->session;
			for (int n = 0; n < MIDI_EVENTS; ++n) {
				if (!d->queued) {
					if (!midi_pop(s->midi_out, &d->pending))
						break;
					int64_t delta = (int64_t)d->pending.frame - (int64_t)player_clock(d->player) + d->player->latency;
					d->due = (uint32_t)clock_ms(NULL) + (delta > 0 ? delta * 1000 / s->sample_rate : 0);
					d->queued = 1;
				}
				if ((int32_t)((uint32_t)clock_ms(NULL) - (uint32_t)d->due) < 0)
					break;
				MidiPacket packet = d->pending;
				d->queued = 0;
				PmTimestamp at = 0;
				for (int i = 0; i < d->count; ++i) {
					Port *p = d->ports + i;
					if (!p->stream || p->input || p->node != packet.node)
						continue;
					PmError result = packet.data[0] == 0xf0
					    ? Pm_WriteSysEx(p->stream, at, packet.data)
					    : Pm_WriteShort(p->stream, at,
					          Pm_Message(packet.data[0], packet.size > 1 ? packet.data[1] : 0,
					              packet.size > 2 ? packet.data[2] : 0));
					if (result < 0)
						d->error = Pm_GetErrorText(result);
				}
			}
		}
		pthread_mutex_unlock(&d->lock);
		nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
	}
}
MidiDevices *midi_devices_new(void) {
	MidiDevices *d = calloc(1, sizeof(*d));
	if (!d)
		return NULL;
	if (Pm_Initialize() != pmNoError) {
		free(d);
		return NULL;
	}
	d->count = Pm_CountDevices();
	d->ports = calloc(d->count ? d->count : 1, sizeof(Port));
	if (!d->ports) {
		Pm_Terminate();
		free(d);
		return NULL;
	}
	for (int i = 0; i < d->count; ++i)
		d->ports[i] = (Port){.node = -1, .input = Pm_GetDeviceInfo(i)->input};
	pthread_mutex_init(&d->lock, NULL);
	if (pthread_create(&d->thread, NULL, poll_midi, d)) {
		pthread_mutex_destroy(&d->lock);
		free(d->ports);
		Pm_Terminate();
		free(d);
		return NULL;
	}
	return d;
}
void midi_devices_stop(MidiDevices *d) {
	if (!d)
		return;
	pthread_mutex_lock(&d->lock);
	d->player = NULL;
	d->queued = 0;
	d->error = NULL;
	for (int i = 0; i < d->count; ++i)
		close_port(d->ports + i);
	pthread_mutex_unlock(&d->lock);
}
void midi_devices_detach(MidiDevices *d) {
	if (!d)
		return;
	midi_devices_stop(d);
	for (int i = 0; i < d->count; ++i)
		d->ports[i].node = -1;
}
void midi_devices_free(MidiDevices *d) {
	if (!d)
		return;
	midi_devices_stop(d);
	pthread_mutex_lock(&d->lock);
	d->closing = 1;
	pthread_mutex_unlock(&d->lock);
	pthread_join(d->thread, NULL);
	pthread_mutex_destroy(&d->lock);
	free(d->ports);
	Pm_Terminate();
	free(d);
}
const char *midi_devices_start(MidiDevices *d, Player *player) {
	if (!d)
		return NULL; // No device service was requested.
	pthread_mutex_lock(&d->lock);
	if (d->player == player) {
		const char *error = d->error;
		pthread_mutex_unlock(&d->lock);
		return error;
	}
	d->error = NULL;
	Session *s = player->session;
	for (int i = 0; i < s->nnodes; ++i)
		atomic_store(&s->midi_capture[i], 0);
	for (int i = 0; i < d->count; ++i) {
		Port *p = d->ports + i;
		if (p->node < 0)
			continue;
		PmError e = p->input ? Pm_OpenInput(&p->stream, i, NULL, 1024, clock_ms, NULL)
		                     : Pm_OpenOutput(&p->stream, i, NULL, 1024, clock_ms, NULL, 0);
		if (e < 0) {
			d->error = Pm_GetErrorText(e);
			break;
		}
		if (p->input)
			Pm_SetFilter(p->stream, PM_FILT_ACTIVE);
		else
			atomic_store(&s->midi_capture[p->node], 1);
	}
	d->player = d->error ? NULL : player;
	const char *error = d->error;
	pthread_mutex_unlock(&d->lock);
	if (error)
		midi_devices_stop(d);
	return error;
}
const char *midi_devices_connect(MidiDevices *d, Session *s, int device, int node) {
	if (!d || device < 0 || device >= d->count || node < -1 || node >= s->nnodes)
		return "Invalid MIDI connection";
	if (node >= 0 && (d->ports[device].input ? !s->nodes[node].midi_in : !s->nodes[node].midi_out))
		return "Node has no MIDI port in this direction";
	d->ports[device].node = node;
	return NULL;
}
const char *midi_devices_error(MidiDevices *d) {
	if (!d)
		return NULL;
	pthread_mutex_lock(&d->lock);
	const char *error = d->error;
	pthread_mutex_unlock(&d->lock);
	return error;
}
char *midi_devices_json(MidiDevices *d) {
	Json json = {0};
	if (d)
		pthread_mutex_lock(&d->lock);
	json_print(&json, "{\"devices\":[");
	for (int i = 0; d && i < d->count; ++i) {
		const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
		json_print(&json, "%s{\"id\":\"%d\",\"name\":", i ? "," : "", i);
		json_string(&json, info->name);
		json_print(&json, ",\"input\":%s,\"node\":%d}", info->input ? "true" : "false", d->ports[i].node);
	}
	json_print(&json, "]}");
	if (d)
		pthread_mutex_unlock(&d->lock);
	if (json.failed) {
		free(json.data);
		return NULL;
	}
	return json.data;
}
