#include "lv2.h"
#include "scan.h"
#include <stdarg.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

LilvNode *lv2_get(LilvWorld *world, const LilvNode *subject, const char *predicate) {
	LilvNode *p = lilv_new_uri(world, predicate);
	LilvNode *result = lilv_world_get(world, subject, p, NULL);
	lilv_node_free(p);
	return result;
}
int lv2_is(LilvWorld *world, const LilvNode *subject, const char *predicate, const char *object) {
	LilvNode *p = lilv_new_uri(world, predicate), *o = lilv_new_uri(world, object);
	int result = lilv_world_ask(world, subject, p, o);
	lilv_node_free(p);
	lilv_node_free(o);
	return result;
}
int lv2_default_state(Lv2Description *d) {
	const LilvNode *uri = lilv_plugin_get_uri(d->plugin);
	return lv2_is(d->world, uri, LV2_CORE__requiredFeature, LV2_STATE__loadDefaultState) ||
	    lv2_is(d->world, uri, LV2_CORE__optionalFeature, LV2_STATE__loadDefaultState);
}
LilvWorld *lv2_world_open(const char *path) {
	LilvWorld *world = lilv_world_new();
	if (!world)
		return NULL;
	size_t size = strlen(path) + 2;
	char *directory = malloc(size);
	if (!directory) {
		lilv_world_free(world);
		return NULL;
	}
	snprintf(directory, size, "%s/", path);
	LilvNode *uri = lilv_new_file_uri(world, NULL, directory);
	free(directory);
	lilv_world_load_bundle(world, uri);
	lilv_node_free(uri);
	return world;
}
static void *open_module(const char *path) {
	return lv2_world_open(path);
}
static void close_module(void *data) {
	lilv_world_free(data);
}
const ModuleOps lv2_module_ops = {open_module, close_module};

static int required(LilvWorld *world, const LilvNode *subject, const char *property, const char *const *supported,
    const char *const *extra, char *error, size_t size) {
	LilvNode *predicate = lilv_new_uri(world, property);
	LilvNodes *values = lilv_world_find_nodes(world, subject, predicate, NULL);
	int ok = 1;
	LILV_FOREACH(nodes, i, values) {
		const LilvNode *value = lilv_nodes_get(values, i);
		const char *uri = lilv_node_as_uri(value);
		int found = 0;
		for (int j = 0; uri && supported[j]; ++j)
			found |= !strcmp(uri, supported[j]);
		for (int j = 0; uri && extra && extra[j]; ++j)
			found |= !strcmp(uri, extra[j]);
		if (!found) {
			snprintf(error, size, "Unsupported %s: %s",
			    !strcmp(property, LV2_CORE__requiredFeature) ? "required feature" : "required option",
			    lilv_node_as_string(value));
			ok = 0;
			break;
		}
	}
	lilv_nodes_free(values);
	lilv_node_free(predicate);
	return ok;
}
static int required_features(LilvWorld *world, const LilvNode *subject, int ui, char *error, size_t size) {
	static const char *common[] = {LV2_URID__map, LV2_URID__unmap, LV2_OPTIONS__options,
	    LV2_BUF_SIZE__boundedBlockLength, LV2_LOG__log, LV2_CORE__hardRTCapable, LV2_WORKER__schedule,
	    LV2_STATE__loadDefaultState, NULL};
	static const char *view[] = {LV2_UI__parent, LV2_UI__resize, LV2_UI__idleInterface, LV2_UI__noUserResize,
	    LV2_UI__fixedSize, LV2_INSTANCE_ACCESS_URI, LV2_DATA_ACCESS_URI, LV2_UI__portMap, NULL};
	static const char *options[] = {LV2_PARAMETERS__sampleRate, LV2_BUF_SIZE__minBlockLength,
	    LV2_BUF_SIZE__maxBlockLength, LV2_BUF_SIZE__sequenceSize, LV2_UI__scaleFactor, NULL};
	return required(world, subject, LV2_CORE__requiredFeature, common, ui ? view : NULL, error, size) &&
	    required(world, subject, LV2_OPTIONS__requiredOption, options, NULL, error, size);
}
int lv2_ui_fixed(Lv2Description *d, const LilvUI *ui) {
	const char *predicates[] = {LV2_CORE__requiredFeature, LV2_CORE__optionalFeature};
	for (int i = 0; i < 2; ++i)
		if (lv2_is(d->world, lilv_ui_get_uri(ui), predicates[i], LV2_UI__noUserResize) ||
		    lv2_is(d->world, lilv_ui_get_uri(ui), predicates[i], LV2_UI__fixedSize))
			return 1;
	return 0;
}
LilvUIs *lv2_uis(Lv2Description *d, const LilvUI **selected) {
	LilvUIs *uis = lilv_plugin_get_uis(d->plugin);
	LilvNode *type = lilv_new_uri(d->world, lv2_ui_type);
	*selected = NULL;
	d->ui_error[0] = 0;
	LILV_FOREACH(uis, i, uis) {
		const LilvUI *ui = lilv_uis_get(uis, i);
		lilv_world_load_resource(d->world, lilv_ui_get_uri(ui));
		if (!lilv_ui_is_a(ui, type)) {
			if (!d->ui_error[0]) {
				const LilvNode *kind = lilv_nodes_get_first(lilv_ui_get_classes(ui));
				snprintf(d->ui_error, sizeof(d->ui_error), "Unsupported UI type: %s",
				    kind ? lilv_node_as_string(kind) : "unknown");
			}
			continue;
		}
		if (required_features(d->world, lilv_ui_get_uri(ui), 1, d->ui_error, sizeof(d->ui_error))) {
			*selected = ui;
			d->ui_error[0] = 0;
			break;
		}
	}
	lilv_node_free(type);
	return uis;
}
static int reject(Lv2Description *d, const char *format, ...) {
	va_list args;
	va_start(args, format);
	vsnprintf(d->error, sizeof(d->error), format, args);
	va_end(args);
	return -1;
}
static double number(const LilvNode *node, double fallback) {
	return node && (lilv_node_is_float(node) || lilv_node_is_int(node)) ? lilv_node_as_float(node) : fallback;
}
int lv2_describe(Lv2Description *d, LilvWorld *world, const char *id) {
	*d = (Lv2Description){.world = world, .midi = -1, .latency = -1};
	LilvNode *uri = lilv_new_uri(world, id);
	d->plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), uri);
	lilv_node_free(uri);
	if (!d->plugin)
		return reject(d, "Plugin not found");
	if (!required_features(world, lilv_plugin_get_uri(d->plugin), 0, d->error, sizeof(d->error)))
		return -1;
	d->count = lilv_plugin_get_num_ports(d->plugin);
	if (!d->count || d->count > LV2_PORT_LIMIT)
		return reject(d, "Invalid port count (maximum %u)", LV2_PORT_LIMIT);
	if (!(d->ports = calloc(d->count, sizeof(*d->ports))))
		return reject(d, "Cannot allocate port descriptions");
	LilvNode *main[2] = {lv2_get(world, lilv_plugin_get_uri(d->plugin), LV2_PORT_GROUPS__mainOutput),
	    lv2_get(world, lilv_plugin_get_uri(d->plugin), LV2_PORT_GROUPS__mainInput)};
	for (uint32_t i = 0; i < d->count && !d->error[0]; ++i) {
		const LilvPort *port = lilv_plugin_get_port_by_index(d->plugin, i);
		const LilvNode *node = lilv_port_get_node(d->plugin, port);
		Lv2Port *p = d->ports + i;
		p->parameter = p->channel = -1;
		p->input = lv2_is(world, node, LILV_NS_RDF "type", LV2_CORE__InputPort);
		if (p->input == lv2_is(world, node, LILV_NS_RDF "type", LV2_CORE__OutputPort)) {
			reject(d, "Port %u must have exactly one direction", i);
			break;
		}
		if (lv2_is(world, node, LILV_NS_RDF "type", LV2_CORE__ControlPort)) {
			p->type = LV2_PORT_CONTROL;
			p->parameter = d->nparams++;
			p->scaled = lv2_is(world, node, LV2_CORE__portProperty, LV2_CORE__sampleRate);
			p->freewheel = p->input && lv2_is(world, node, LV2_CORE__designation, LV2_CORE__freeWheeling);
			if (!p->input &&
			    (lv2_is(world, node, LV2_CORE__designation, LV2_CORE__latency) ||
			        lv2_is(world, node, LV2_CORE__portProperty, LV2_CORE__reportsLatency)))
				d->latency = i;
		} else if (lv2_is(world, node, LILV_NS_RDF "type", LV2_CORE__AudioPort)) {
			p->type = LV2_PORT_AUDIO;
			LilvNode *group = lv2_get(world, node, LV2_PORT_GROUPS__group);
			if (!main[p->input] || lilv_node_equals(group, main[p->input]))
				p->channel = p->input ? d->main_inputs++ : d->main_outputs++;
			lilv_node_free(group);
			if (p->input)
				++d->inputs;
			else
				++d->outputs;
		} else if (lv2_is(world, node, LILV_NS_RDF "type", LV2_ATOM__AtomPort) &&
		    lv2_is(world, node, LV2_ATOM__bufferType, LV2_ATOM__Sequence)) {
			p->type = LV2_PORT_ATOM;
			p->midi = lv2_is(world, node, LV2_ATOM__supports, LV2_MIDI__MidiEvent);
			p->time = p->input && lv2_is(world, node, LV2_ATOM__supports, LV2_TIME__Position);
			LilvNode *size = lv2_get(world, node, LV2_RESIZE_PORT__minimumSize);
			double minimum = size ? number(size, NAN) : 0;
			if (!isfinite(minimum) || minimum < 0 || minimum != floor(minimum))
				reject(d, "Port %u has an invalid minimum buffer size", i);
			else if (minimum > LV2_ATOM_LIMIT)
				reject(d, "Port %u requires %.0f bytes (limit %u)", i, minimum, LV2_ATOM_LIMIT);
			else
				p->capacity = ((uint32_t)fmax(LV2_ATOM_DEFAULT, minimum) + 7u) & ~7u;
			lilv_node_free(size);
			if (p->midi) {
				if (!p->input)
					reject(d, "MIDI output routing is not supported (port %u)", i);
				else if (d->midi >= 0)
					reject(d, "Multiple MIDI inputs are not supported");
				else
					d->midi = i;
			}
		} else if (!lv2_is(world, node, LV2_CORE__portProperty, LV2_CORE__connectionOptional))
			reject(d, "Unsupported required port type (port %u)", i);
	}
	lilv_node_free(main[0]);
	lilv_node_free(main[1]);
	if (d->error[0])
		return -1;
	if (!d->main_outputs)
		return reject(d, "Plugins without audio outputs are not supported");
	if (d->main_outputs > 2 || d->main_inputs > 2)
		return reject(
		    d, "Main audio buses must be mono or stereo (got %d inputs, %d outputs)", d->main_inputs, d->main_outputs);
	if (d->inputs > MAX_INPUTS || d->outputs > 64)
		return reject(d, "Too many audio channels (%d inputs, %d outputs)", d->inputs, d->outputs);
	int in = d->main_inputs, out = d->main_outputs;
	for (uint32_t i = 0; i < d->count; ++i)
		if (d->ports[i].type == LV2_PORT_AUDIO && d->ports[i].channel < 0)
			d->ports[i].channel = d->ports[i].input ? in++ : out++;
	return 0;
}
int lv2_metadata(Lv2Description *d, Json *j, const char *path) {
	LilvNode *name = lilv_plugin_get_name(d->plugin);
	const char *id = lilv_node_as_uri(lilv_plugin_get_uri(d->plugin));
	const LilvUI *ui;
	LilvUIs *uis = lv2_uis(d, &ui);
	json_print(j, "{\"id\":");
	json_string(j, id);
	json_print(j, ",\"binary\":");
	json_string(j, path);
	if (ui)
		json_print(j, ",\"ui-binary\":\"lv2\"");
	if (d->ui_error[0]) {
		json_print(j, ",\"ui-error\":");
		json_string(j, d->ui_error);
	}
	json_print(j,
	    ",\"layout\":{\"input-channels\":%d,\"output-channels\":%d,\"midi-bus\":%d,"
	    "\"input-offset\":0,\"input-slots\":%d,\"dsp-to-ui-size\":0,\"ui-to-dsp-size\":0},"
	    "\"product\":{\"format\":\"lv2\",\"id\":",
	    d->main_inputs, d->main_outputs, d->midi, d->inputs);
	json_string(j, id);
	json_print(j, ",\"name\":");
	json_string(j, name ? lilv_node_as_string(name) : id);
	lilv_node_free(name);
	if (ui)
		json_print(j, ",\"ui\":{\"userResizable\":%s}", lv2_ui_fixed(d, ui) ? "false" : "true");
	lilv_uis_free(uis);
	json_print(j, ",\"parameters\":[");
	int defaults = lv2_default_state(d);
	int n = 0;
	for (uint32_t i = 0; i < d->count; ++i) {
		Lv2Port *p = d->ports + i;
		if (p->type != LV2_PORT_CONTROL)
			continue;
		const LilvPort *port = lilv_plugin_get_port_by_index(d->plugin, i);
		const LilvNode *node = lilv_port_get_node(d->plugin, port);
		LilvNode *def, *min, *max;
		lilv_port_get_range(d->plugin, port, &def, &min, &max);
		double low = number(min, 0), high = number(max, i == (uint32_t)d->latency ? 3840000 : 1);
		double value = number(def, fmax(low, fmin(high, 0)));
		LilvNode *initial = defaults && p->input ? lv2_get(d->world, node, LV2_PRESETS__value) : NULL;
		int has_initial = initial != NULL;
		double start = number(initial, NAN);
		lilv_node_free(initial);
		lilv_node_free(def);
		lilv_node_free(min);
		lilv_node_free(max);
		if (!isfinite(low) || !isfinite(high) || !isfinite(value) || low > high || value < low || value > high ||
		    fabs(low) > FLT_MAX || fabs(high) > FLT_MAX ||
		    (has_initial &&
		        (!isfinite(start) || fabs(start) > FLT_MAX || (!p->scaled && (start < low || start > high)))))
			return -1;
		name = lilv_port_get_name(d->plugin, port);
		json_print(j, "%s{\"id\":", n++ ? "," : "");
		json_string(j, lilv_node_as_string(lilv_port_get_symbol(d->plugin, port)));
		// State port values are absolute; only range/default metadata uses sample-rate scaling.
		if (has_initial)
			json_print(j, ",\"initialValue\":%.17g", start);
		json_print(j, ",\"nativeId\":%u,\"sampleRate\":%s,\"automatable\":%s,\"name\":", i,
		    p->scaled ? "true" : "false",
		    p->freewheel || lv2_is(d->world, node, LV2_CORE__portProperty, LV2_PORT_PROPS__notAutomatic) ? "false"
		                                                                                                 : "true");
		json_string(j, name ? lilv_node_as_string(name) : "Control");
		lilv_node_free(name);
		int integer = lv2_is(d->world, node, LV2_CORE__portProperty, LV2_CORE__integer);
		int toggle = lv2_is(d->world, node, LV2_CORE__portProperty, LV2_CORE__toggled);
		int hidden = p->freewheel || lv2_is(d->world, node, LV2_CORE__portProperty, LV2_PORT_PROPS__notOnGUI);
		int log = lv2_is(d->world, node, LV2_CORE__portProperty, LV2_PORT_PROPS__logarithmic);
		json_print(j,
		    ",\"minimum\":%.17g,\"maximum\":%.17g,\"defaultValue\":%.17g,\"direction\":\"%s\","
		    "\"integer\":%s,\"toggled\":%s,\"hidden\":%s,\"map\":\"%s\",\"scalePoints\":{",
		    low, high, value, p->input ? "input" : "output", integer || toggle ? "true" : "false",
		    toggle ? "true" : "false", hidden ? "true" : "false", log && low > 0 ? "logarithmic" : "linear");
		LilvScalePoints *points = lilv_port_get_scale_points(d->plugin, port);
		unsigned count = 0;
		LILV_FOREACH(scale_points, k, points) {
			const LilvScalePoint *point = lilv_scale_points_get(points, k);
			if (count++)
				json_print(j, ",");
			json_string(j, lilv_node_as_string(lilv_scale_point_get_label(point)));
			json_print(j, ":%.17g", number(lilv_scale_point_get_value(point), 0));
		}
		lilv_scale_points_free(points);
		json_print(j, "},\"list\":%s}",
		    count && lv2_is(d->world, node, LV2_CORE__portProperty, LV2_CORE__enumeration) ? "true" : "false");
	}
	json_print(j, "]}}");
	return j->failed ? -1 : 0;
}
int lv2_scan(Json *json, const char *path) {
	LilvWorld *world = lv2_world_open(path);
	if (!world)
		return -1;
	const LilvPlugins *plugins = lilv_world_get_all_plugins(world);
	json_print(json, "[");
	unsigned n = 0;
	LILV_FOREACH(plugins, i, plugins) {
		const char *id = lilv_node_as_uri(lilv_plugin_get_uri(lilv_plugins_get(plugins, i)));
		Lv2Description d;
		Json entry = {0};
		if (!lv2_describe(&d, world, id) && !lv2_metadata(&d, &entry, path))
			json_print(json, "%s%s", n++ ? "," : "", entry.data);
		else {
			LilvNode *name = lilv_plugin_get_name(lilv_plugins_get(plugins, i));
			json_print(json, "%s", n++ ? "," : "");
			scan_error(json, "lv2", path, id, name ? lilv_node_as_string(name) : id,
			    d.error[0] ? d.error : "Invalid parameter metadata");
			lilv_node_free(name);
		}
		free(d.ports);
		free(entry.data);
	}
	json_print(json, "]");
	lilv_world_free(world);
	return json->failed ? -1 : 0;
}
