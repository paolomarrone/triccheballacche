#include "engine.h"
#include <math.h>

int open_engine(Engine *e, const char *path, const PluginConfig *c, unsigned sample_rate) {
	if (e->dsp || !sample_rate || sample_rate > 384000 || c->input < 0 || c->input > 2 || c->output < 1 ||
	    c->output > 2 || c->inputs < c->input || c->inputs > MAX_INPUTS || c->input_offset < 0 ||
	    c->input_offset > MAX_INPUTS || c->input_offset + c->input > c->inputs || c->midi < -1 || c->nparams < 0 ||
	    c->nparams > MAX_PARAMS)
		return -1;
	for (int i = 0; i < c->nparams; ++i)
		if (!(c->outputs & (UINT64_C(1) << i)) && !isfinite(c->defaults[i]))
			return -1;
	DSP *dsp = open_dsp(path, c, sample_rate, BLOCK);
	if (!dsp)
		return -1;
	e->dsp = dsp;
	e->config = *c;
	return 0;
}

void close_engine(Engine *e) {
	close_dsp(e->dsp);
	*e = (Engine){0};
}

// Split at events and adapt interleaved stereo to the plugin's planar buffers.
void render(Engine *e, float *out, const float *in, size_t frames) {
	static const float silence[BLOCK];
	float input[2][BLOCK], output[2][BLOCK];
	const PluginConfig *c = &e->config;
	while (frames) {
		while (e->next < e->count && e->events[e->next].time <= e->time) {
			const Event *v = &e->events[e->next++];
			if (v->parameter >= 0)
				set_dsp(e->dsp, v->parameter, v->value);
			else if (c->midi >= 0)
				midi_dsp(e->dsp, c->midi, v->midi);
		}
		size_t n = frames < BLOCK ? frames : BLOCK;
		if (e->next < e->count && e->events[e->next].time - e->time < n)
			n = e->events[e->next].time - e->time;
		const float *x[MAX_INPUTS] = {0}; // Optional disconnected buses receive NULL.
		if (c->input)
			x[c->input_offset] = in ? in : silence;
		if (c->input == 2)
			x[c->input_offset + 1] = silence;
		float *y[2] = {out, output[1]};
		if (c->input == 2 && in) {
			for (size_t i = 0; i < n; ++i) {
				input[0][i] = in[2 * i];
				input[1][i] = in[2 * i + 1];
			}
			x[c->input_offset] = input[0];
			x[c->input_offset + 1] = input[1];
		}
		if (c->output == 2)
			y[0] = output[0];
		process_dsp(e->dsp, c->inputs ? x : NULL, y, n);
		if (c->output == 2)
			for (size_t i = 0; i < n; ++i) {
				out[2 * i] = y[0][i];
				out[2 * i + 1] = y[1][i];
			}
		e->time += n;
		frames -= n;
		out += n * c->output;
		if (in)
			in += n * c->input;
	}
}
