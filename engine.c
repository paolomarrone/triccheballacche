#include "engine.h"

void close_engine(Engine *e) {
	if (e->instance) e->module->api->destroy(e->instance);
	tibia_loader_unload(e->module);
	*e = (Engine){0};
}

int open_engine(Engine *e, const char *path) {
	if (!(e->module = tibia_loader_load(path))) return -1;
	if (!(e->instance = e->module->api->create(SAMPLE_RATE, NULL))) { close_engine(e); return -1; }
	return 0;
}

// Split at events and adapt interleaved stereo to the plugin's planar buffers.
void render(Engine *e, float *out, const float *in, size_t frames) {
	static const float silence[BLOCK];
	float input[2][BLOCK], output[2][BLOCK];
	const TibiaModule *m = e->module;
	while (frames) {
		while (e->next < e->count && e->events[e->next].time <= e->time) {
			const Event *v = &e->events[e->next++];
			if (v->parameter >= 0)
				e->module->api->set_parameter(e->instance, v->parameter, v->value);
			else if (e->module->api->midi_msg_in)
				e->module->api->midi_msg_in(e->instance, e->module->midi, v->midi);
		}
		size_t n = frames < BLOCK ? frames : BLOCK;
		if (e->next < e->count && e->events[e->next].time - e->time < n)
			n = e->events[e->next].time - e->time;
		const float *x[2] = {in ? in : silence, silence};
		float *y[2] = {out, output[1]};
		if (m->input == 2 && in) {
			for (size_t i = 0; i < n; ++i) { input[0][i] = in[2 * i]; input[1][i] = in[2 * i + 1]; }
			x[0] = input[0]; x[1] = input[1];
		}
		if (m->output == 2) y[0] = output[0];
		m->api->process(e->instance, m->input ? x : NULL, y, n);
		if (m->output == 2)
			for (size_t i = 0; i < n; ++i) { out[2 * i] = y[0][i]; out[2 * i + 1] = y[1][i]; }
		e->time += n;
		frames -= n;
		out += n * m->output;
		if (in) in += n * m->input;
	}
}
