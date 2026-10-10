#include "plugin.h"
#include "util.h"
#include <math.h>
#include <stdlib.h>

int plugin_config_valid(const PluginConfig *c) {
	if (c->input < 0 || c->input > 2 || c->output < 1 || c->output > 2 || c->inputs < c->input ||
	    c->inputs > MAX_INPUTS || c->input_offset < 0 || c->input_offset > MAX_INPUTS ||
	    c->input_offset + c->input > c->inputs || c->midi < -1 || c->nparams < 0 || c->nparams > MAX_PARAMS ||
	    (c->nparams && !c->parameters) || (c->format != PLUGIN_PERONE && c->format != PLUGIN_CLAP) ||
	    (c->format == PLUGIN_CLAP && (!c->id || !*c->id)))
		return 0;
	for (int i = 0; i < c->nparams; ++i)
		if (!(c->parameters[i].flags & PARAM_OUTPUT) && !isfinite(c->parameters[i].value))
			return 0;
	return 1;
}

int open_plugin(Plugin *e, const char *path, const PluginConfig *c, unsigned sample_rate) {
	if (e->dsp || !sample_rate || sample_rate > 384000 || !plugin_config_valid(c))
		return -1;
	if (plugin_config_copy(&e->config, c))
		return -1;
	DSP *dsp = open_dsp(e->modules, path, &e->config, sample_rate, BLOCK);
	if (!dsp) {
		plugin_config_free(&e->config);
		return -1;
	}
	e->dsp = dsp;
	return 0;
}

void close_plugin(Plugin *e) {
	close_dsp(e->dsp);
	plugin_config_free(&e->config);
	*e = (Plugin){0};
}

// Adapt interleaved audio to the plugin's planar buffers.
int render_plugin(Plugin *e, float *out, const float *in, size_t frames) {
	static const float silence[BLOCK];
	float input[2][BLOCK], output[2][BLOCK];
	const PluginConfig *c = &e->config;
	while (frames) {
		size_t n = frames < BLOCK ? frames : BLOCK;
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
		if (process_dsp(e->dsp, c->inputs ? x : NULL, y, n, &e->time))
			return -1;
		if (c->output == 2)
			for (size_t i = 0; i < n; ++i) {
				out[2 * i] = y[0][i];
				out[2 * i + 1] = y[1][i];
			}
		frames -= n;
		out += n * c->output;
		if (in)
			in += n * c->input;
	}
	return 0;
}

void plugin_info_free(PluginInfo *info) {
	free(info->name);
	free(info->product);
	free(info->ui);
	*info = (PluginInfo){0};
}

int plugin_parameter(const PluginConfig *config, size_t index, double *value) {
	if (index >= (size_t)config->nparams || (config->parameters[index].flags & PARAM_OUTPUT) || !isfinite(*value))
		return -1;
	const Parameter *p = config->parameters + index;
	double low = p->minimum, high = p->maximum;
	if (p->flags & PARAM_INTEGER) {
		*value = round(*value);
		low = ceil(low);
		high = floor(high);
	}
	*value = fmin(high, fmax(low, *value));
	return 0;
}

void plugin_config_free(PluginConfig *config) {
	free(config->id);
	free(config->parameters);
	*config = (PluginConfig){0};
}

int plugin_config_copy(PluginConfig *out, const PluginConfig *source) {
	*out = *source;
	out->id = source->id ? copy_string(source->id) : NULL;
	out->parameters = source->nparams ? malloc(source->nparams * sizeof(Parameter)) : NULL;
	if ((source->id && !out->id) || (source->nparams && !out->parameters)) {
		plugin_config_free(out);
		return -1;
	}
	if (source->nparams)
		memcpy(out->parameters, source->parameters, source->nparams * sizeof(Parameter));
	return 0;
}
