#include "loader.h"
#include <emscripten.h>
#include <stdlib.h>

const size_t dsp_max_message = 4096;

// clang-format off
EM_JS(int, wasm_open,
    (const char *path, unsigned rate, int inputs, int output, int midi, int nparams, const float *defaults,
        const uint8_t *output_flags, size_t capacity, size_t to_ui, size_t to_dsp), {
	try {
		return Module.perone.open({path: UTF8ToString(path), sampleRate: rate,
		    inputChannels: inputs, outputChannels: output, midiBus: midi,
		    parameters: HEAPF32.subarray(defaults / 4, defaults / 4 + nparams),
		    outputParameters: Uint8Array.from(HEAPU8.subarray(output_flags, output_flags + nparams)), capacity, toUi: to_ui, toDsp: to_dsp});
	} catch (error) {
		(Module.printErr || console.error)(String(error));
		return 0;
	}
});

EM_JS(void, sync_dsp, (DSP *id, DSP *paired), { Module.perone.sync(id, paired); });

EM_JS(void, wasm_close, (void *id), { Module.perone.close(id); });
EM_JS(void, wasm_set, (void *id, size_t parameter, double value), { Module.perone.set(id, parameter, value); });
EM_JS(void, wasm_reset, (void *id), { Module.perone.reset(id); });
EM_JS(void, wasm_midi, (void *id, size_t bus, const uint8_t *message), { Module.perone.midi(id, bus, message); });
EM_JS(void, wasm_process, (void *id, const float **inputs, float **outputs, size_t frames), {
	Module.perone.process(id, inputs, outputs, frames);
});
// clang-format on

void modules_free(Modules *modules) {
	(void)modules; // Compiled modules belong to the JavaScript host.
}

DSP *open_dsp(Modules *modules, const char *path, const PluginConfig *c, unsigned sample_rate, size_t capacity) {
	(void)modules;
	if (c->format != PLUGIN_PERONE)
		return NULL;
	float *defaults = c->nparams ? malloc(c->nparams * sizeof(float)) : NULL;
	uint8_t *outputs = c->nparams ? malloc(c->nparams) : NULL;
	if (c->nparams && (!defaults || !outputs)) {
		free(defaults);
		free(outputs);
		return NULL;
	}
	for (int i = 0; i < c->nparams; ++i) {
		defaults[i] = c->parameters[i].value;
		outputs[i] = !!(c->parameters[i].flags & PARAM_OUTPUT);
	}
	DSP *dsp = (DSP *)(uintptr_t)wasm_open(
	    path, sample_rate, c->inputs, c->output, c->midi, c->nparams, defaults, outputs, capacity, c->to_ui, c->to_dsp);
	free(defaults);
	free(outputs);
	return dsp;
}

void close_dsp(DSP *dsp) {
	if (dsp)
		wasm_close(dsp);
}

void set_dsp(DSP *dsp, size_t parameter, double value) {
	wasm_set(dsp, parameter, value);
}

void reset_dsp(DSP *dsp) {
	wasm_reset(dsp);
}

void midi_dsp(DSP *dsp, size_t bus, const uint8_t *message) {
	wasm_midi(dsp, bus, message);
}

int process_dsp(DSP *dsp, const float **inputs, float **outputs, size_t frames, const ProcessTime *time) {
	(void)time;
	wasm_process(dsp, inputs, outputs, frames);
	return 0;
}

int poll_dsp(DSP *dsp) {
	(void)dsp;
	return 0;
}
void idle_dsp(DSP *dsp, const ProcessTime *time) {
	(void)dsp;
	(void)time;
}
int restart_dsp(DSP *dsp) {
	(void)dsp;
	return 0;
}
int state_dsp(DSP *dsp, const char *path, int write) {
	(void)dsp;
	(void)path;
	(void)write;
	return -1;
}

uint32_t latency_dsp(DSP *dsp) {
	(void)dsp;
	return 0;
}
