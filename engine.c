#include "engine.h"
#include <stdlib.h>

void close_engine(Engine *e) {
	if (e->instance) e->module->fini(e->instance);
	free(e->memory);
	free(e->instance);
	tibia_loader_unload(e->module);
	*e = (Engine){0};
}

int open_engine(Engine *e, const char *path) {
	if (!(e->module = tibia_loader_load(path))) return -1;
	if (!(e->instance = e->module->new())) return -1;
	e->module->init(e->instance, NULL);
	e->module->set_sample_rate(e->instance, SAMPLE_RATE);
	size_t size = e->module->mem_req(e->instance);
	if (size && !(e->memory = malloc(size))) return -1;
	e->module->mem_set(e->instance, e->memory);
	e->module->reset(e->instance);
	return 0;
}

// Events are chronological; mono buffers can be processed directly.
void render(Engine *e, float *out, const float *in, size_t frames) {
	static const float silence[BLOCK];
	while (frames) {
		while (e->next < e->count && e->events[e->next].time <= e->time) {
			const Event *v = &e->events[e->next++];
			if (v->parameter >= 0)
				e->module->set_parameter(e->instance, v->parameter, v->value);
			else if (e->module->midi_msg_in)
				e->module->midi_msg_in(e->instance, 0, v->midi);
		}
		size_t n = frames < BLOCK ? frames : BLOCK;
		if (e->next < e->count && e->events[e->next].time - e->time < n)
			n = e->events[e->next].time - e->time;
		const float *input = in ? in : silence;
		e->module->process(e->instance, &input, &out, n);
		e->time += n;
		frames -= n;
		out += n;
		if (in) in += n;
	}
}
