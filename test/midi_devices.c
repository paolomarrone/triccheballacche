#include "native/midi.h"
#include "support.h"
#include <portmidi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void peer(int commands, int replies) {
	assert(Pm_Initialize() == pmNoError);
	int output = Pm_CreateVirtualOutput("tb-midi-test-keyboard", NULL, NULL);
	int input = Pm_CreateVirtualInput("tb-midi-test-synth", NULL, NULL);
	if (output < 0 || input < 0) {
		char skip = 's';
		assert(write(replies, &skip, 1) == 1);
		_exit(77);
	}
	PortMidiStream *out, *in;
	assert(Pm_OpenOutput(&out, output, NULL, 256, NULL, NULL, 0) == pmNoError);
	assert(Pm_OpenInput(&in, input, NULL, 256, NULL, NULL) == pmNoError);
	Pm_SetFilter(in, PM_FILT_ACTIVE);
	char command = 'r';
	assert(write(replies, &command, 1) == 1 && read(commands, &command, 1) == 1);
	assert(command == 'n');
	assert(Pm_WriteShort(out, 0, Pm_Message(0x93, 65, 95)) == pmNoError);
	assert(Pm_WriteShort(out, 0, Pm_Message(0xb3, 7, 77)) == pmNoError);
	assert(Pm_WriteSysEx(out, 0, (unsigned char[]){0xf0, 0x7d, 1, 2, 0xf7}) == pmNoError);
	unsigned found = 0;
	for (int i = 0; i < 2000 && found != 7; ++i) {
		PmEvent events[32];
		int n = Pm_Read(in, events, 32);
		assert(n >= 0);
		for (int j = 0; j < n; ++j) {
			if (events[j].message == Pm_Message(0x93, 65, 95))
				found |= 1;
			if (events[j].message == Pm_Message(0xb3, 7, 77))
				found |= 2;
			if ((uint32_t)events[j].message == 0x02017df0)
				found |= 4;
		}
		usleep(1000);
	}
	assert(found == 7);
	command = 'd';
	assert(write(replies, &command, 1) == 1);
	assert(read(commands, &command, 1) == 1 && command == 'q');
	Pm_Close(out);
	Pm_Close(in);
	Pm_Terminate();
	_exit(0);
}
int main(void) {
	int commands[2], replies[2];
	assert(!pipe(commands) && !pipe(replies));
	pid_t child = fork();
	assert(child >= 0);
	if (!child) {
		close(commands[1]);
		close(replies[0]);
		peer(commands[0], replies[1]);
	}
	close(commands[0]);
	close(replies[1]);
	char command;
	assert(read(replies[0], &command, 1) == 1);
	if (command == 's') {
		waitpid(child, NULL, 0);
		puts("SKIP: MIDI virtual ports unavailable");
		return 0;
	}
	MidiDevices *devices = midi_devices_new();
	assert(devices);
	int input = -1, output = -1;
	for (int i = 0; i < Pm_CountDevices(); ++i) {
		const PmDeviceInfo *p = Pm_GetDeviceInfo(i);
		if (p->input && strstr(p->name, "tb-midi-test-keyboard"))
			input = i;
		if (p->output && strstr(p->name, "tb-midi-test-synth"))
			output = i;
	}
	assert(input >= 0 && output >= 0);
	Session s = {.sample_rate = 48000};
	Output config;
	assert(!prepare_session(
	    &s, &config, "test/midi.janet", "(def a (daw/midi)) (daw/midi-output a) (daw/end 5)", NULL, NULL));
	Player player = {.session = &s};
	assert(!midi_devices_connect(devices, &s, input, 0));
	assert(!midi_devices_connect(devices, &s, output, 0));
	assert(!midi_devices_start(devices, &player));
	command = 'n';
	assert(write(commands[1], &command, 1) == 1);
	float audio[96];
	for (int i = 0; i < 250; ++i) {
		assert(!session_monitor(&s, audio, 48));
		assert(!s.time);
		atomic_store(&player.clock, s.clock);
		assert(!midi_devices_error(devices));
		usleep(1000);
	}
	assert(read(replies[0], &command, 1) == 1 && command == 'd');
	midi_devices_stop(devices);
	session_midi_panic(&s);
	midi_devices_free(devices);
	session_free(&s);
	command = 'q';
	assert(write(commands[1], &command, 1) == 1);
	int status;
	assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
	close(commands[1]);
	close(replies[0]);
	puts("OK: PortMidi virtual devices, keyboard → graph → output, controllers, SysEx and cleanup");
	return 0;
}
