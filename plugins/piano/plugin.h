// Sampled piano with a quieter detuned unison and an embedded PCM bank.
#include "piano.h"

typedef Piano plugin;

static int plugin_init(void *instance, const plugin_callbacks *callbacks) {
	(void)callbacks;
	*(plugin *)instance = (plugin){.rate = 44100, .gain = .6f, .detune = 11, .blend = .32f};
	return 0;
}

static void plugin_fini(void *instance) {
	piano_reset(instance);
}

static void plugin_set_sample_rate(void *instance, float rate) {
	plugin *p = instance;
	if (isfinite(rate) && rate >= 1 && rate <= 384000) {
		p->rate = rate;
		piano_reset(p);
	}
}

static size_t plugin_mem_req(void *instance) {
	(void)instance;
	return 0;
}

static void plugin_mem_set(void *instance, void *memory) {
	(void)instance;
	(void)memory;
}

static void plugin_reset(void *instance) {
	piano_reset(instance);
}

static void plugin_set_parameter(void *instance, size_t index, float value) {
	plugin *p = instance;
	if (!isfinite(value))
		return;
	switch (index) {
	case plugin_parameter_gain:
		p->gain = fmaxf(0, fminf(2, value));
		break;
	case plugin_parameter_detune:
		p->detune = fmaxf(0, fminf(30, value));
		for (int i = 0; i < PIANO_VOICES; ++i)
			if (p->voices[i].region && p->voices[i].layer)
				p->voices[i].step = p->voices[i].base_step * exp2(p->detune / 1200.0);
		break;
	case plugin_parameter_blend:
		p->blend = fmaxf(0, fminf(1, value));
		break;
	}
}

static float plugin_get_parameter(void *instance, size_t index) {
	plugin *p = instance;
	switch (index) {
	case plugin_parameter_gain:
		return p->gain;
	case plugin_parameter_detune:
		return p->detune;
	case plugin_parameter_blend:
		return p->blend;
	default:
		return 0;
	}
}

static void plugin_process(void *instance, const float **inputs, float **outputs, size_t count) {
	(void)inputs;
	piano_process(instance, outputs[0], outputs[1], count);
}

static void plugin_midi_msg_in(void *instance, size_t bus, const uint8_t *data) {
	(void)bus;
	piano_midi(instance, data);
}
