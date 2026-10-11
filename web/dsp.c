#include "dsp.h"
#include <emscripten.h>
#include <stdlib.h>

const size_t dsp_max_message = 4096;

// clang-format off
EM_JS(int, wasm_open,
    (const char *path, unsigned rate, int inputs, int output, int midi, int nparams, const float *defaults,
        const uint8_t *flags, const double *limits, size_t capacity, size_t to_ui, size_t to_dsp), {
	try {
		return Module.perone.open({path: UTF8ToString(path), sampleRate: rate,
		    inputChannels: inputs, outputChannels: output, midiBus: midi,
		    parameters: HEAPF32.subarray(defaults / 4, defaults / 4 + nparams),
		    flags: Uint8Array.from(HEAPU8.subarray(flags, flags + nparams)),
		    outputParameters: Uint8Array.from(HEAPU8.subarray(flags, flags + nparams), flag => flag & 1),
		    minimum: Float64Array.from(HEAPF64.subarray(limits / 8, limits / 8 + nparams)),
		    maximum: Float64Array.from(HEAPF64.subarray(limits / 8 + nparams, limits / 8 + nparams * 2)),
		    capacity, toUi: to_ui, toDsp: to_dsp});
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
EM_JS(void, wasm_transport, (void *id, int playing, float bpm, double beat), {
	Module.perone.transport(id, playing, bpm, beat);
});
EM_JS(void, wasm_process, (void *id, const float **inputs, float **outputs, size_t frames, int playing, float bpm, double beat), {
	Module.perone.transport(id, playing, bpm, beat);
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
	uint8_t *flags = c->nparams ? malloc(c->nparams) : NULL;
	double *limits = c->nparams ? malloc(c->nparams * 2 * sizeof(double)) : NULL;
	if (c->nparams && (!defaults || !flags || !limits)) {
		free(defaults);
		free(flags);
		free(limits);
		return NULL;
	}
	for (int i = 0; i < c->nparams; ++i) {
		defaults[i] = c->parameters[i].value;
		flags[i] = c->parameters[i].flags;
		limits[i] = c->parameters[i].minimum;
		limits[c->nparams + i] = c->parameters[i].maximum;
	}
	DSP *dsp = (DSP *)(uintptr_t)wasm_open(path, sample_rate, c->inputs, c->output, c->midi, c->nparams, defaults,
	    flags, limits, capacity, c->to_ui, c->to_dsp);
	free(defaults);
	free(flags);
	free(limits);
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
	wasm_process(dsp, inputs, outputs, frames, time ? time->playing : -1, time ? time->bpm : 0, time ? time->beat : 0);
	return 0;
}

int poll_dsp(DSP *dsp) {
	(void)dsp;
	return 0;
}
void idle_dsp(DSP *dsp, const ProcessTime *time) {
	if (dsp && time)
		wasm_transport(dsp, time->playing, time->bpm, time->beat);
}
int restart_dsp(DSP *dsp) {
	(void)dsp;
	return 0;
}
// clang-format off
EM_JS(int, wasm_state, (DSP *id, unsigned char **data, size_t *size, int write), {
	try {
		if (write) {
			const bytes = Module.perone.state(id), pointer = _malloc(Math.max(1, bytes.length));
			if (!pointer) return -1;
			HEAPU8.set(bytes, pointer);
			HEAPU32[data / 4] = pointer;
			HEAPU32[size / 4] = bytes.length;
		} else {
			const pointer = HEAPU32[data / 4], length = HEAPU32[size / 4];
			Module.perone.state(id, HEAPU8.slice(pointer, pointer + length));
		}
		return 0;
	} catch (error) {
		(Module.printErr || console.error)(String(error));
		return -1;
	}
});
// clang-format on
int state_dsp(DSP *dsp, State *state, int write) {
	if (!dsp || !state || state->length > STATE_LIMIT || (state->length && !state->data))
		return -1;
	State buffer = write ? (State){0} : *state;
	int result = wasm_state(dsp, &buffer.data, &buffer.length, write);
	if (write && !result) {
		buffer.capacity = buffer.length;
		state_free(state);
		*state = buffer;
	}
	return result;
}

uint32_t latency_dsp(DSP *dsp) {
	(void)dsp;
	return 0;
}
