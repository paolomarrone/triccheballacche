#ifndef POSIX_MIDI_H
#define POSIX_MIDI_H
#include "player.h"
// PortMidi and its polling thread belong to the application, never the DSP graph.
typedef struct MidiDevices MidiDevices;
MidiDevices *midi_devices_new(void);
void midi_devices_free(MidiDevices *devices);
char *midi_devices_json(MidiDevices *devices);
// Stop MIDI before pausing/replacing/freeing the borrowed player.
const char *midi_devices_play(MidiDevices *devices, Player *player);
void midi_devices_stop(MidiDevices *devices);
// Audio stopped. A device connects to one node; graph edges provide fan-out/merge.
const char *midi_devices_connect(MidiDevices *devices, Session *session, int device, int node);
void midi_devices_detach(MidiDevices *devices);
const char *midi_devices_error(MidiDevices *devices);
#endif
