#include "mda.h"
#include <new>
#include "dsp.h"

struct plugin {
	AudioEffectX *dsp;
	float rate, values[plugin_parameter__count];
	bool prepared;
};

static int plugin_init(plugin *p, const plugin_callbacks *) {
	new (p) plugin{};
	p->dsp = createEffectInstance(true);
	p->rate = 44100;
	for (int i = 0; i < p->dsp->numParams; ++i)
		p->values[i] = p->dsp->getParameter(i);
	return 0;
}
static void plugin_fini(plugin *p) {
	delete p->dsp;
	p->~plugin();
}
static void plugin_set_sample_rate(plugin *p, float rate) {
	p->rate = rate;
}
static size_t plugin_mem_req(plugin *) {
	return 0;
}
static void plugin_mem_set(plugin *, void *) {
}
static void plugin_reset(plugin *p) {
	// MDA's suspend() is not a full reset for all effects. Recreate at preparation time.
	if (p->prepared) {
		delete p->dsp;
		p->dsp = createEffectInstance(true);
	}
	p->dsp->setSampleRate(p->rate);
	for (int i = 0; i < p->dsp->numParams; ++i)
		p->dsp->setParameter(i, p->values[i]);
	p->dsp->resume();
	p->prepared = true;
}
static void plugin_set_parameter(plugin *p, size_t index, float value) {
	p->values[index] = value;
	p->dsp->setParameter(index, value);
}
static float plugin_get_parameter(plugin *p, size_t index) {
#ifdef MDA_METER
	auto *dsp = static_cast<mdaSpecMeter *>(p->dsp);
	if (index == 0)
		return dsp->Lpeak;
	if (index == 1)
		return dsp->Rpeak;
	if (index == 2)
		return sqrtf(dsp->Lrms);
	if (index == 3)
		return sqrtf(dsp->Rrms);
	if (index == 4)
		return dsp->Corr;
	return dsp->band[(index - 5) / 13][(index - 5) % 13];
#else
	return p->dsp->getParameter(index);
#endif
}
static void plugin_midi_msg_in(plugin *p, size_t, const uint8_t *data) {
	// Perone delivers MIDI at the current sample; the port applies events immediately.
	VstMidiEvent event;
	memcpy(event.midiData, data, 3);
	VstEvents events;
	events.numEvents = 1;
	events.events[0] = &event;
	p->dsp->processEvents(&events);
	if ((data[0] & 0xf0) == 0xc0)
		for (int i = 0; i < p->dsp->numParams; ++i)
			p->values[i] = p->dsp->getParameter(i);
}
static void plugin_process(plugin *p, const float **inputs, float **outputs, size_t frames) {
	p->dsp->setBlockSize(static_cast<int>(frames));
	p->dsp->processReplacing(const_cast<float **>(inputs), outputs, static_cast<int>(frames));
}
