#include "loader.h"
#include <emscripten.h>

// clang-format off
EM_JS(int, wasm_open,
    (const char *path, unsigned rate, int inputs, int output, int midi, int nparams, const float *defaults,
        uint32_t outputs_low, uint32_t outputs_high, size_t capacity), {
	try {
		return Module.perone.open({path: UTF8ToString(path), sampleRate: rate,
		    inputChannels: inputs, outputChannels: output, midiBus: midi,
		    parameters: HEAPF32.subarray(defaults / 4, defaults / 4 + nparams),
		    outputMask: [outputs_low, outputs_high], capacity});
	} catch (error) {
		(Module.printErr || console.error)(String(error));
		return 0;
	}
});

EM_JS(void, wasm_close, (void *id), { Module.perone.close(id); });
EM_JS(void, wasm_set, (void *id, size_t parameter, float value), { Module.perone.set(id, parameter, value); });
EM_JS(void, wasm_reset, (void *id), { Module.perone.reset(id); });
EM_JS(void, wasm_midi, (void *id, size_t bus, const uint8_t *message), { Module.perone.midi(id, bus, message); });
EM_JS(void, wasm_process, (void *id, const float **inputs, float **outputs, size_t frames), {
	Module.perone.process(id, inputs, outputs, frames);
});
// clang-format on

DSP *open_dsp(const char *path, const PluginConfig *c, unsigned sample_rate, size_t capacity) {
	return (DSP *)(uintptr_t)wasm_open(path, sample_rate, c->inputs, c->output, c->midi, c->nparams, c->defaults,
	    (uint32_t)c->outputs, (uint32_t)(c->outputs >> 32), capacity);
}

void close_dsp(DSP *dsp) {
	if (dsp)
		wasm_close(dsp);
}

void set_dsp(DSP *dsp, size_t parameter, float value) {
	wasm_set(dsp, parameter, value);
}

void reset_dsp(DSP *dsp) {
	wasm_reset(dsp);
}

void midi_dsp(DSP *dsp, size_t bus, const uint8_t *message) {
	wasm_midi(dsp, bus, message);
}

void process_dsp(DSP *dsp, const float **inputs, float **outputs, size_t frames) {
	wasm_process(dsp, inputs, outputs, frames);
}
