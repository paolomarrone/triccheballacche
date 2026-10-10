#include "vst3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void vst_id(char out[33], const Steinberg_TUID id) {
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < 16; ++i) {
		unsigned byte = (unsigned char)id[i];
		out[2 * i] = hex[byte >> 4];
		out[2 * i + 1] = hex[byte & 15];
	}
	out[32] = 0;
}
void vst_string(char *out, size_t capacity, const Steinberg_char16 *in, size_t length) {
	size_t n = 0;
	for (size_t i = 0; i < length && in[i]; ++i) {
		uint32_t c = (uint16_t)in[i];
		if (c >= 0xd800 && c <= 0xdbff && i + 1 < length && (uint16_t)in[i + 1] >= 0xdc00 &&
		    (uint16_t)in[i + 1] <= 0xdfff) {
			c = 0x10000 + ((c - 0xd800) << 10) + (uint16_t)in[++i] - 0xdc00;
		} else if (c >= 0xd800 && c <= 0xdfff)
			c = 0xfffd;
		unsigned bytes = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
		if (n + bytes >= capacity)
			break;
		if (bytes == 1)
			out[n++] = c;
		else {
			out[n++] = (0xf00 >> bytes & 0xff) | (c >> (6 * (bytes - 1)));
			for (unsigned j = bytes - 1; j; --j)
				out[n++] = 0x80 | ((c >> (6 * (j - 1))) & 63);
		}
	}
	if (capacity)
		out[n] = 0;
}
static int compare(const void *a, const void *b) {
	uint32_t x = ((const VstParameter *)a)->id, y = ((const VstParameter *)b)->id;
	return (x > y) - (x < y);
}
int vst_parameter(Vst *v, uint32_t id) {
	VstParameter key = {.id = id};
	VstParameter *p = v->nparams ? bsearch(&key, v->by_id, v->nparams, sizeof(key), compare) : NULL;
	return p ? p->index : -1;
}
double vst_scale(const struct Steinberg_Vst_ParameterInfo *p) {
	return p->stepCount > 0 ? p->stepCount : 1;
}
int vst_parameters(Vst *v) {
	if (!v->controller)
		return 0;
	v->nparams = v->controller->lpVtbl->getParameterCount(v->controller);
	if (v->nparams < 0 || v->nparams > MAX_PARAMS)
		return -1;
	v->info = calloc(v->nparams, sizeof(*v->info));
	v->by_id = calloc(v->nparams, sizeof(*v->by_id));
	v->controller_values = calloc(v->nparams, sizeof(double));
	if (v->nparams && (!v->info || !v->by_id || !v->controller_values))
		return -1;
	for (int i = 0; i < v->nparams; ++i) {
		struct Steinberg_Vst_ParameterInfo *p = v->info + i;
		if (v->controller->lpVtbl->getParameterInfo(v->controller, i, p) != Steinberg_kResultOk ||
		    p->id == UINT32_MAX || p->stepCount < 0 || !isfinite(p->defaultNormalizedValue) ||
		    p->defaultNormalizedValue < 0 || p->defaultNormalizedValue > 1)
			return -1;
		v->by_id[i] = (VstParameter){p->id, i};
		v->controller_values[i] = v->controller->lpVtbl->getParamNormalized(v->controller, p->id);
	}
	qsort(v->by_id, v->nparams, sizeof(*v->by_id), compare);
	for (int i = 1; i < v->nparams; ++i)
		if (v->by_id[i - 1].id == v->by_id[i].id)
			return -1;
	return 0;
}
int vst_layout(Vst *v, int *input, int *output, int *midi) {
	Steinberg_Vst_IComponent *c = v->component;
	*input = *output = 0;
	*midi = -1;
	int counts[2], slots[2] = {0};
	for (int dir = 0; dir < 2; ++dir) {
		counts[dir] = c->lpVtbl->getBusCount(c, Steinberg_Vst_MediaTypes_kAudio, dir);
		if (counts[dir] < 0 || counts[dir] > (dir ? 64 : MAX_INPUTS) || (dir && !counts[dir]))
			return -1;
		for (int i = 0; i < counts[dir]; ++i) {
			struct Steinberg_Vst_BusInfo b;
			if (c->lpVtbl->getBusInfo(c, Steinberg_Vst_MediaTypes_kAudio, dir, i, &b) != Steinberg_kResultOk ||
			    b.channelCount < 1 || b.channelCount > 64)
				return -1;
			if (!i && b.busType == Steinberg_Vst_BusTypes_kMain) {
				if (b.channelCount > 2)
					return -1;
				*(dir ? output : input) = b.channelCount;
			} else if (dir && !i)
				return -1;
			struct Steinberg_Vst_AudioBusBuffers *buffers = dir ? v->outputs : v->inputs;
			int previous = dir ? v->noutputs : v->ninputs;
			if (buffers && (previous != counts[dir] || buffers[i].numChannels != b.channelCount))
				return -1;
			slots[dir] += b.channelCount;
		}
	}
	if (slots[0] > MAX_INPUTS || slots[1] > 256)
		return -1;
	int events = c->lpVtbl->getBusCount(c, Steinberg_Vst_MediaTypes_kEvent, Steinberg_Vst_BusDirections_kInput);
	if (events < 0 || events > 1 ||
	    c->lpVtbl->getBusCount(c, Steinberg_Vst_MediaTypes_kEvent, Steinberg_Vst_BusDirections_kOutput))
		return -1;
	if (events)
		*midi = 0;
	v->ninputs = counts[0];
	v->noutputs = counts[1];
	v->input_slots = slots[0];
	v->output_slots = slots[1];
	v->midi = *midi;
	return 0;
}
static void scale_points(Vst *v, int index, Json *j) {
	const struct Steinberg_Vst_ParameterInfo *p = v->info + index;
	if (p->stepCount <= 0 || p->stepCount > 128)
		return;
	json_print(j, ",\"toggled\":%s", p->stepCount == 1 ? "true" : "false");
	char labels[129][512];
	for (int k = 0; k <= p->stepCount; ++k) {
		Steinberg_Vst_String128 text = {0};
		if (v->controller->lpVtbl->getParamStringByValue(v->controller, p->id, (double)k / p->stepCount, text) !=
		    Steinberg_kResultOk)
			return;
		vst_string(labels[k], sizeof(labels[k]), text, 128);
		if (!labels[k][0])
			return;
		for (int n = 0; n < k; ++n)
			if (!strcmp(labels[k], labels[n]))
				return; // Keep an integer slider when labels cannot identify every step.
	}
	json_print(j, ",\"list\":%s,\"scalePoints\":{",
	    p->flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsList ? "true" : "false");
	for (int k = 0; k <= p->stepCount; ++k) {
		json_print(j, "%s", k ? "," : "");
		json_string(j, labels[k]);
		json_print(j, ":%d", k);
	}
	json_print(j, "}");
}
static int metadata(Vst *v, Json *j, const char *path, const struct Steinberg_PClassInfo *info, const char *vendor) {
	int input, output, midi;
	if (vst_layout(v, &input, &output, &midi))
		return -1;
	Steinberg_IPlugView *view = v->controller ? v->controller->lpVtbl->createView(v->controller, "editor") : NULL;
	int gui = view && view->lpVtbl->isPlatformTypeSupported(view, vst_window_type) == Steinberg_kResultOk;
	vst_release(view);
	char id[33];
	vst_id(id, v->cid);
	json_print(j, "{\"id\":");
	json_string(j, id);
	json_print(j, ",\"binary\":");
	json_string(j, path);
	if (gui)
		json_print(j, ",\"ui-binary\":\"vst3\"");
	json_print(j,
	    ",\"layout\":{\"input-channels\":%d,\"output-channels\":%d,\"midi-bus\":%d,"
	    "\"input-offset\":0,\"input-slots\":%d,\"dsp-to-ui-size\":0,\"ui-to-dsp-size\":0},"
	    "\"product\":{\"format\":\"vst3\",\"id\":\"%s\",\"name\":",
	    input, output, midi, v->input_slots, id);
	json_string(j, info->name);
	json_print(j, ",\"vendor\":");
	json_string(j, vendor);
	if (gui)
		json_print(j, ",\"ui\":{\"userResizable\":true}");
	json_print(j, ",\"parameters\":[");
	for (int i = 0; i < v->nparams; ++i) {
		const struct Steinberg_Vst_ParameterInfo *p = v->info + i;
		double scale = vst_scale(p), initial = v->controller_values[i];
		if (!isfinite(initial) || initial < 0 || initial > 1)
			return -1;
		char label[512];
		vst_string(label, sizeof(label), p->title, 128);
		json_print(j, "%s{\"id\":\"%u\",\"nativeId\":%u,\"name\":", i ? "," : "", p->id, p->id);
		json_string(j, label);
		json_print(j,
		    ",\"minimum\":0,\"maximum\":%.17g,\"defaultValue\":%.17g,\"initialValue\":%.17g,"
		    "\"integer\":%s,\"direction\":\"%s\",\"automatable\":%s,\"hidden\":%s",
		    scale, p->stepCount ? round(p->defaultNormalizedValue * scale) : p->defaultNormalizedValue,
		    p->stepCount ? round(initial * scale) : initial, p->stepCount ? "true" : "false",
		    p->flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsReadOnly ? "output" : "input",
		    p->flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kCanAutomate ? "true" : "false",
		    p->flags & Steinberg_Vst_ParameterInfo_ParameterFlags_kIsHidden ? "true" : "false");
		scale_points(v, i, j);
		json_print(j, "}");
	}
	json_print(j, "]}}");
	return j->failed ? -1 : 0;
}
int vst_scan(Json *j, const char *path) {
	Module *m = module_open(NULL, path, PLUGIN_VST3);
	if (!m)
		return -1;
	Steinberg_IPluginFactory *f = vst_factory(m);
	struct Steinberg_PFactoryInfo factory = {0};
	int count = f->lpVtbl->countClasses(f), found = 0;
	if (count < 0 || count > 4096 || f->lpVtbl->getFactoryInfo(f, &factory) != Steinberg_kResultOk)
		goto fail;
	factory.vendor[sizeof(factory.vendor) - 1] = 0;
	json_print(j, "[");
	for (int i = 0; i < count; ++i) {
		struct Steinberg_PClassInfo info = {0};
		if (f->lpVtbl->getClassInfo(f, i, &info) != Steinberg_kResultOk)
			goto fail;
		info.category[sizeof(info.category) - 1] = 0;
		info.name[sizeof(info.name) - 1] = 0;
		if (strcmp(info.category, "Audio Module Class"))
			continue;
		char id[33];
		vst_id(id, info.cid);
		Vst v = {0};
		Json description = {0};
		int created = vst_create(&v, m, id);
		int error = created || metadata(&v, &description, module_path(m), &info, factory.vendor);
		vst_destroy(&v);
		if (error)
			fprintf(stderr, "[Scan] %s: %s\n", info.name,
			    created ? "VST3 initialization/parameters failed" : "unsupported VST3 layout/parameters");
		else
			json_print(j, "%s%s", found++ ? "," : "", description.data);
		free(description.data);
	}
	json_print(j, "]");
	module_close(m);
	return j->failed ? -1 : 0;
fail:
	module_close(m);
	return -1;
}
