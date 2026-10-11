#include "clap.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int compare_parameter(const void *a, const void *b) {
	clap_id x = ((const ClapParameter *)a)->id, y = ((const ClapParameter *)b)->id;
	return (x > y) - (x < y);
}
int clap_parameters(Clap *c) {
	if (!c->params)
		return 0;
	if (!c->params->count || !c->params->get_info || !c->params->get_value || !c->params->flush)
		return -1;
	c->nparams = c->params->count(c->plugin);
	if (c->nparams > MAX_PARAMS)
		return -1;
	c->info = calloc(c->nparams, sizeof(*c->info));
	c->by_id = calloc(c->nparams, sizeof(*c->by_id));
	if (c->nparams && (!c->info || !c->by_id))
		return -1;
	for (uint32_t i = 0; i < c->nparams; ++i) {
		clap_param_info_t *p = c->info + i;
		if (!c->params->get_info(c->plugin, i, p) || p->id == CLAP_INVALID_ID || !memchr(p->name, 0, sizeof(p->name)) ||
		    !memchr(p->module, 0, sizeof(p->module)) || !isfinite(p->min_value) || !isfinite(p->max_value) ||
		    !isfinite(p->default_value) || p->min_value > p->max_value || p->default_value < p->min_value ||
		    p->default_value > p->max_value)
			return -1;
		c->by_id[i] = (ClapParameter){p->id, i};
	}
	qsort(c->by_id, c->nparams, sizeof(*c->by_id), compare_parameter);
	for (uint32_t i = 1; i < c->nparams; ++i)
		if (c->by_id[i - 1].id == c->by_id[i].id)
			return -1;
	return 0;
}
int clap_layout(Clap *c, int *input, int *output, int *midi) {
	*input = *output = 0;
	*midi = -1;
	if (!c->audio || !c->audio->count || !c->audio->get)
		return -1;
	uint32_t ni = c->audio->count(c->plugin, true), no = c->audio->count(c->plugin, false);
	if (!no || ni > MAX_INPUTS || no > 64)
		return -1;
	uint32_t slots[2] = {0};
	for (int in = 0; in < 2; ++in) {
		uint32_t count = in ? ni : no;
		for (uint32_t i = 0; i < count; ++i) {
			clap_audio_port_info_t p;
			if (!c->audio->get(c->plugin, i, in, &p) || !p.channel_count || p.channel_count > 64)
				return -1;
			if (!i) {
				if (p.flags & CLAP_AUDIO_PORT_IS_MAIN) {
					if (p.channel_count > 2)
						return -1;
					*(in ? input : output) = p.channel_count;
				} else if (!in)
					return -1;
			} else if (p.flags & CLAP_AUDIO_PORT_IS_MAIN)
				return -1;
			slots[in] += p.channel_count;
		}
	}
	if (slots[1] > MAX_INPUTS || slots[0] > 256)
		return -1;
	if (c->inputs || c->outputs) {
		if (ni != c->ninputs || no != c->noutputs || slots[1] != c->input_slots || slots[0] != c->output_slots)
			return -1;
		for (int in = 0; in < 2; ++in)
			for (uint32_t i = 0; i < (in ? ni : no); ++i) {
				clap_audio_port_info_t p;
				if (!c->audio->get(c->plugin, i, in, &p) ||
				    p.channel_count != (in ? c->inputs : c->outputs)[i].channel_count)
					return -1;
			}
	}
	c->ninputs = ni;
	c->noutputs = no;
	c->input_slots = slots[1];
	c->output_slots = slots[0];
	if (c->notes) {
		// Note outputs can remain disconnected, including those exposed by
		// NIH-plug instruments. event_push accepts NOTE_END and declines MIDI.
		if (!c->notes->count || !c->notes->get || c->notes->count(c->plugin, true) > 1)
			return -1;
		if (c->notes->count(c->plugin, true)) {
			clap_note_port_info_t p;
			if (!c->notes->get(c->plugin, 0, true, &p))
				return -1;
			c->dialect = p.supported_dialects & CLAP_NOTE_DIALECT_CLAP ? CLAP_NOTE_DIALECT_CLAP
			    : p.supported_dialects & CLAP_NOTE_DIALECT_MIDI        ? CLAP_NOTE_DIALECT_MIDI
			                                                           : 0;
			if (!c->dialect)
				return -1;
			*midi = 0;
		}
	}
	return 0;
}
int clap_metadata(Clap *c, Json *j, const char *path) {
	int input, output, midi;
	if (clap_layout(c, &input, &output, &midi))
		return -1;
	int gui = c->gui && c->gui->is_api_supported && c->gui->is_api_supported(c->plugin, clap_window_api, false);
	json_print(j, "{\"id\":");
	json_string(j, c->plugin->desc->id);
	json_print(j, ",\"binary\":");
	json_string(j, path);
	if (gui)
		json_print(j, ",\"ui-binary\":\"clap\"");
	json_print(j,
	    ",\"layout\":{\"input-channels\":%d,\"output-channels\":%d,\"midi-bus\":%d,"
	    "\"input-offset\":0,\"input-slots\":%d,\"dsp-to-ui-size\":0,\"ui-to-dsp-size\":0},\"product\":{\"format\":\"clap\",\"name\":",
	    input, output, midi, c->input_slots);
	json_string(j, c->plugin->desc->name);
	json_print(j, ",\"id\":");
	json_string(j, c->plugin->desc->id);
	json_print(j, ",\"vendor\":");
	json_string(j, c->plugin->desc->vendor);
	json_print(j, ",\"version\":");
	json_string(j, c->plugin->desc->version);
	if (gui)
		json_print(j, ",\"ui\":{\"userResizable\":true}");
	json_print(j, ",\"parameters\":[");
	for (uint32_t i = 0; i < c->nparams; ++i) {
		const clap_param_info_t *p = c->info + i;
		double initial = p->default_value;
		if (!c->params->get_value(c->plugin, p->id, &initial) || !isfinite(initial) || initial < p->min_value ||
		    initial > p->max_value)
			return -1;
		// NIH-plug's float32 normalization can leave integer controls a few
		// ulps away from their step. Publish discrete defaults to Janet.
		double declared = p->default_value;
		if (p->flags & CLAP_PARAM_IS_STEPPED) {
			initial = round(initial);
			declared = round(declared);
		}
		json_print(j, "%s{\"id\":\"%u\",\"nativeId\":%u,\"name\":", i ? "," : "", p->id, p->id);
		json_string(j, p->name);
		json_print(j, ",\"module\":");
		json_string(j, p->module);
		json_print(j,
		    ",\"minimum\":%.17g,\"maximum\":%.17g,\"defaultValue\":%.17g,\"initialValue\":%.17g,\"direction\":\"%s\","
		    "\"integer\":%s,\"automatable\":%s,\"hidden\":%s}",
		    p->min_value, p->max_value, declared, initial, p->flags & CLAP_PARAM_IS_READONLY ? "output" : "input",
		    p->flags & CLAP_PARAM_IS_STEPPED ? "true" : "false",
		    p->flags & CLAP_PARAM_IS_AUTOMATABLE ? "true" : "false",
		    p->flags & CLAP_PARAM_IS_HIDDEN ? "true" : "false");
	}
	json_print(j, "]}}");
	return j->failed ? -1 : 0;
}
