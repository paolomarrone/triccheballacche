#include "lv2.h"
#include "plugin.h"
#include <sratom/sratom.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Port symbols and Turtle atoms keep state independent of port indices and runtime URIDs.
typedef struct {
	uint32_t key;
	LV2_Atom *atom;
} Property;
typedef struct {
	Lv2 *plugin;
	State *buffer;
	Sratom *sratom;
	Property *properties;
	uint32_t count;
} Snapshot;

static int property_order(const void *a, const void *b) {
	uint32_t x = ((const Property *)a)->key, y = ((const Property *)b)->key;
	return (x > y) - (x < y);
}

static SerdNode subject(void) {
	return serd_node_from_string(SERD_URI, (const uint8_t *)"urn:triccheballacche:state");
}
static SerdNode predicate(void) {
	return serd_node_from_string(SERD_URI, (const uint8_t *)"urn:triccheballacche:value");
}
static LV2_State_Status store(
    LV2_State_Handle handle, uint32_t key, const void *value, size_t size, uint32_t type, uint32_t flags) {
	Snapshot *s = handle;
	if (!(flags & LV2_STATE_IS_POD) || !(flags & LV2_STATE_IS_PORTABLE))
		return LV2_STATE_ERR_BAD_FLAGS;
	if (!size || !value)
		return LV2_STATE_ERR_BAD_TYPE;
	if (size > STATE_LIMIT || s->count == MAX_PARAMS)
		return LV2_STATE_ERR_NO_SPACE;
	const char *uri = lv2_unmap_uri(s->plugin, key);
	if (!uri)
		return LV2_STATE_ERR_BAD_TYPE;
	SerdNode sub = subject(), pred = predicate();
	char *text = sratom_to_turtle(s->sratom, &s->plugin->unmap, "file:///", &sub, &pred, type, size, value);
	if (!text)
		return LV2_STATE_ERR_BAD_TYPE;
	const char *body = text;
	int error = state_string(s->buffer, &uri, 1) || state_string(s->buffer, &body, 1);
	free(text);
	if (error)
		return LV2_STATE_ERR_NO_SPACE;
	++s->count;
	return LV2_STATE_SUCCESS;
}
static const void *retrieve(LV2_State_Handle handle, uint32_t key, size_t *size, uint32_t *type, uint32_t *flags) {
	Snapshot *s = handle;
	const Property needle = {.key = key};
	const Property *p = s->count ? bsearch(&needle, s->properties, s->count, sizeof(*p), property_order) : NULL;
	if (!p)
		return NULL;
	if (size)
		*size = p->atom->size;
	if (type)
		*type = p->atom->type;
	if (flags)
		*flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
	return p->atom + 1;
}
static char *map_path(LV2_State_Map_Path_Handle handle, const char *path) {
	(void)handle;
	// Presets reference existing assets. Project asset packaging is a separate operation.
	if (!path || path[0] != '/')
		return NULL;
	return strdup(path);
}
static void free_path(LV2_State_Free_Path_Handle handle, char *path) {
	(void)handle;
	free(path);
}
static int ports(Lv2 *l, State *buffer, double *values, int write) {
	DSP *dsp = l->dsp;
	uint32_t count = 0, expected = 0;
	for (int i = 0; i < dsp->config.nparams; ++i)
		if (!(dsp->config.parameters[i].flags & PARAM_OUTPUT))
			++expected;
	count = expected;
	if (state_u32(buffer, &count, write) || count != expected)
		return -1;
	for (uint32_t i = 0, index = 0; i < count; ++i) {
		const LilvPort *port;
		const char *symbol = NULL;
		float value = 0;
		if (write) {
			while (dsp->config.parameters[index].flags & PARAM_OUTPUT)
				++index;
			uint32_t id = dsp->config.parameters[index++].id;
			port = lilv_plugin_get_port_by_index(l->description.plugin, id);
			symbol = lilv_node_as_string(lilv_port_get_symbol(l->description.plugin, port));
			Lv2Port *p = l->description.ports + id;
			value = p->value / (p->scaled ? l->rate : 1);
		}
		uint32_t bits;
		memcpy(&bits, &value, sizeof(bits));
		if (state_string(buffer, &symbol, write) || state_u32(buffer, &bits, write))
			return -1;
		if (!write) {
			LilvNode *name = lilv_new_string(l->description.world, symbol);
			port = name ? lilv_plugin_get_port_by_symbol(l->description.plugin, name) : NULL;
			lilv_node_free(name);
			if (!port)
				return -1;
			Lv2Port *p = l->description.ports + lilv_port_get_index(l->description.plugin, port);
			if (p->type != LV2_PORT_CONTROL || !p->input || p->parameter < 0 || !isnan(values[p->parameter]))
				return -1;
			memcpy(&value, &bits, sizeof(value));
			double adjusted = value * (p->scaled ? l->rate : 1);
			if (plugin_parameter(&dsp->config, p->parameter, &adjusted))
				return -1;
			values[p->parameter] = adjusted;
		}
	}
	return 0;
}
int lv2_state(DSP *dsp, State *buffer, int write) {
	Lv2 *l = dsp->instance;
	const LV2_State_Interface *api = lilv_instance_get_extension_data(l->instance, LV2_STATE__interface);
	if (api && (!api->save || !api->restore))
		return STATE_UNSUPPORTED;
	const char *version = "TBLV2:1", *uri = dsp->config.id;
	if (state_string(buffer, &version, write) || strcmp(version, "TBLV2:1") || state_string(buffer, &uri, write) ||
	    strcmp(uri, dsp->config.id))
		return -1;
	Snapshot s = {.plugin = l, .buffer = buffer, .sratom = sratom_new(&l->map)};
	double *values = dsp->config.nparams ? malloc(dsp->config.nparams * sizeof(double)) : NULL;
	int result = -1;
	if (!s.sratom || (dsp->config.nparams && !values))
		goto done;
	for (int i = 0; i < dsp->config.nparams; ++i)
		values[i] = NAN;
	if (ports(l, buffer, values, write))
		goto done;
	size_t count_offset = buffer->position;
	if (state_u32(buffer, &s.count, write) || s.count > MAX_PARAMS || (!api && s.count))
		goto done;
	if (!write && s.count) {
		s.properties = calloc(s.count, sizeof(*s.properties));
		if (!s.properties)
			goto done;
		for (uint32_t i = 0; i < s.count; ++i) {
			const char *key, *text;
			if (state_string(buffer, &key, 0) || state_string(buffer, &text, 0))
				goto done;
			Property *p = s.properties + i;
			p->key = lv2_map_uri(l, key);
			SerdNode sub = subject(), pred = predicate();
			p->atom = sratom_from_turtle(s.sratom, "file:///", &sub, &pred, text);
			if (!p->key || !p->atom || p->atom->size > STATE_LIMIT)
				goto done;
		}
		qsort(s.properties, s.count, sizeof(*s.properties), property_order);
		for (uint32_t i = 1; i < s.count; ++i)
			if (s.properties[i - 1].key == s.properties[i].key)
				goto done;
	}
	if (!write && buffer->position != buffer->length)
		goto done;
	LV2_State_Map_Path mapping = {NULL, map_path, map_path};
	LV2_State_Free_Path freeing = {NULL, free_path};
	LV2_Feature map = {LV2_STATE__mapPath, &mapping}, release = {LV2_STATE__freePath, &freeing};
	const LV2_Feature *features[sizeof(l->feature_ptrs) / sizeof(*l->feature_ptrs) + 2];
	size_t n = 0;
	while (l->feature_ptrs[n]) {
		features[n] = l->feature_ptrs[n];
		++n;
	}
	features[n++] = &map;
	features[n++] = &release;
	features[n] = NULL;
	lv2_worker_wait(l);
	// State callbacks require the instantiation thread and no concurrent run/worker calls.
	LV2_Handle instance = lilv_instance_get_handle(l->instance);
	uint32_t flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
	int error = api ? (write ? api->save(instance, store, &s, flags, features)
	                         : api->restore(instance, retrieve, &s, flags, features))
	                : 0;
	lv2_worker_wait(l);
	if (error || buffer->failed)
		goto done;
	if (write) {
		size_t end = buffer->position;
		buffer->position = count_offset;
		if (state_u32(buffer, &s.count, 1))
			goto done;
		buffer->position = end;
	} else {
		l->dirty = 1;
		for (int i = 0; i < dsp->config.nparams; ++i)
			if (!isnan(values[i]))
				set_dsp(dsp, i, values[i]);
	}
	result = 0;
done:
	if (s.properties)
		for (uint32_t i = 0; i < s.count; ++i)
			free(s.properties[i].atom);
	free(s.properties);
	if (s.sratom)
		sratom_free(s.sratom);
	free(values);
	return result;
}
