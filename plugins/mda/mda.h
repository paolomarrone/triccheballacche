// Source-level adapter for the MDA DSP classes. This is not a VST implementation.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using VstInt32 = int32_t;
using VstInt16 = int16_t;
using audioMasterCallback = bool;
enum VstPlugCategory { kPlugCategUnknown, kPlugCategEffect, kPlugCategSynth, kPlugCategGenerator };
enum { kVstMidiType = 1, kVstPinIsActive = 1, kVstPinIsStereo = 2 };
#define DECLARE_VST_DEPRECATED(name) name
struct VstMidiEvent {
	int type = kVstMidiType, deltaFrames = 0;
	char midiData[4] = {};
};
using VstEvent = VstMidiEvent;
struct VstEvents {
	int numEvents = 0;
	VstEvent *events[1];
};
struct VstPinProperties {
	char label[64];
	int flags;
};

class AudioEffectX {
  public:
	int numPrograms, numParams, numInputs = 0, numOutputs = 0, curProgram = 0, midiInputs = 0;
	float sampleRate = 44100;
	AudioEffectX(audioMasterCallback, int programs, int parameters) : numPrograms(programs), numParams(parameters) {
	}
	virtual ~AudioEffectX() = default;
	virtual void setSampleRate(float rate) {
		sampleRate = rate;
	}
	float getSampleRate() const {
		return sampleRate;
	}
	virtual void setBlockSize(int) {
	}
	virtual void suspend() {
	}
	virtual void resume() {
	}
	virtual void setProgram(int program) {
		curProgram = program;
	}
	virtual void getProgramName(char *name) {
		*name = 0;
	}
	virtual bool getProgramNameIndexed(int, int, char *) {
		return false;
	}
	virtual void setParameter(int, float) = 0;
	virtual float getParameter(int) = 0;
	virtual void getParameterName(int, char *) = 0;
	virtual void getParameterDisplay(int, char *) = 0;
	virtual void getParameterLabel(int, char *) = 0;
	virtual void processReplacing(float **, float **, int) = 0;
	virtual int processEvents(VstEvents *) {
		return 0;
	}
	virtual int getNumMidiInputChannels() {
		return midiInputs;
	}
	void setNumInputs(int n) {
		numInputs = n;
	}
	void setNumOutputs(int n) {
		numOutputs = n;
	}
	void setUniqueID(int) {
	}
	void canMono() {
	}
	void canProcessReplacing() {
	}
	void isSynth() {
		midiInputs = 1;
	}
	void wantEvents() {
	}
};
using AudioEffect = AudioEffectX;
