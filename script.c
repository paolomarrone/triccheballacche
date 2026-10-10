#include "script.h"
#include "discovery.h"
#include "util.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void janet_json(JanetTable *env);
static const char perone_source[] =
#include "build/generated/perone.inc"
    ;

static const char plugin_source[] =
#include "build/generated/plugin.inc"
    ;

static Janet scan(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 1);
	char *json = scan_plugins(janet_getcstring(argv, 0));
	if (!json)
		janet_panic("Cannot scan plugin (see diagnostics)");
	Janet value = janet_cstringv(json);
	free(json);
	return value;
}

JanetTable *script_env(unsigned sample_rate) {
	janet_init();
	JanetTable *env = janet_core_env(NULL), *json = janet_table(0);
	script_trace(env);
	janet_json(json);
	Janet decode, encode;
	janet_resolve(json, janet_csymbol("decode"), &decode);
	janet_resolve(json, janet_csymbol("encode"), &encode);
	janet_def(env, "json/decode", decode, NULL);
	janet_def(env, "json/encode", encode, NULL);
	janet_def(env, "host/binary-suffix", janet_cstringv(PERONE_SUFFIX), NULL);
	janet_def(env, "host/platform", janet_cstringv(PERONE_PLATFORM), NULL);
	janet_def(env, "host/max-params", janet_wrap_integer(MAX_PARAMS), NULL);
	janet_def(env, "host/sample-rate", janet_wrap_number(sample_rate), NULL);
	janet_def(env, "host/max-inputs", janet_wrap_integer(MAX_INPUTS), NULL);
	janet_def(env, "host/max-message", janet_wrap_number(dsp_max_message), NULL);
	janet_def(env, "host/scan", janet_wrap_cfunction(scan), NULL);
	if (janet_dostring(env, perone_source, "lib/perone.janet", NULL) ||
	    janet_dostring(env, plugin_source, "lib/plugin.janet", NULL)) {
		janet_deinit();
		return NULL;
	}
	return env;
}

static int layout_integer(Janet layout, const char *name) {
	Janet value = janet_get(layout, janet_ckeywordv(name));
	return janet_getinteger(&value, 0);
}

void script_config(Janet bundle, Janet defaults, PluginConfig *config) {
	Janet layout = janet_get(bundle, janet_ckeywordv("layout"));
	janet_getdictionary(&layout, 0);
	JanetView values = janet_getindexed(&defaults, 0);
	Janet parameters = janet_get(bundle, janet_ckeywordv("parameters"));
	JanetView params = janet_getindexed(&parameters, 0);
	if (params.len > MAX_PARAMS || params.len != values.len)
		janet_panic("invalid plugin configuration");
	*config = (PluginConfig){.input = layout_integer(layout, "input-channels"),
	    .output = layout_integer(layout, "output-channels"),
	    .midi = layout_integer(layout, "midi-bus"),
	    .input_offset = layout_integer(layout, "input-offset"),
	    .inputs = layout_integer(layout, "input-slots"),
	    .to_ui = layout_integer(layout, "dsp-to-ui-size"),
	    .to_dsp = layout_integer(layout, "ui-to-dsp-size"),
	    .nparams = params.len};
	Janet id = janet_get(bundle, janet_ckeywordv("id"));
	if (!janet_checktype(id, JANET_NIL)) {
		Janet product = janet_get(bundle, janet_ckeywordv("product"));
		Janet format = janet_get(product, janet_ckeywordv("format"));
		const char *name = janet_getcstring(&format, 0);
		config->format = !strcmp(name, "clap") ? PLUGIN_CLAP
		    : !strcmp(name, "lv2")             ? PLUGIN_LV2
		    : !strcmp(name, "vst3")            ? PLUGIN_VST3
		                                       : -1;
		config->id = copy_string(janet_getcstring(&id, 0));
		if (!config->id)
			janet_panic("out of memory");
	}
	config->parameters = params.len ? calloc(params.len, sizeof(Parameter)) : NULL;
	if (params.len && !config->parameters) {
		plugin_config_free(config);
		janet_panic("out of memory");
	}
	for (int i = 0; i < params.len; ++i) {
		Janet p = params.items[i], low = janet_get(p, janet_ckeywordv("min")),
		      high = janet_get(p, janet_ckeywordv("max")), native_id = janet_get(p, janet_ckeywordv("native-id"));
		Parameter *out = config->parameters + i;
		out->id = janet_getnumber(&native_id, 0);
		out->minimum = janet_getnumber(&low, 0);
		out->maximum = janet_getnumber(&high, 0);
		if (janet_truthy(janet_get(p, janet_ckeywordv("integer"))))
			out->flags |= PARAM_INTEGER;
		if (janet_truthy(janet_get(p, janet_ckeywordv("automatable"))))
			out->flags |= PARAM_AUTOMATABLE;
		if (janet_checktype(values.items[i], JANET_NIL))
			out->flags |= PARAM_OUTPUT;
		else
			out->value = janet_getnumber(values.items, i);
	}
}

void script_info(Janet bundle, Janet encoded, PluginInfo *info) {
	Janet product = janet_get(bundle, janet_ckeywordv("product"));
	Janet name = janet_get(product, janet_ckeywordv("name"));
	info->name = copy_string(janet_checktype(name, JANET_STRING) ? (const char *)janet_unwrap_string(name) : "Perone");
	info->product = copy_string(janet_getcstring(&encoded, 0));
	if (!info->name || !info->product)
		janet_panic("out of memory");
	Janet metadata = janet_get(product, janet_ckeywordv("ui"));
	if (!janet_checktype(metadata, JANET_NIL)) {
		Janet path = janet_get(bundle, janet_ckeywordv("ui-binary"));
		info->ui = copy_string(janet_getcstring(&path, 0));
		if (!info->ui)
			janet_panic("out of memory");
		info->resizable = janet_truthy(janet_get(metadata, janet_ckeywordv("userResizable")));
	}
}

typedef struct {
	char **binary;
	PluginConfig *config;
	PluginInfo *info;
} Bundle;

static Janet config_native(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 6);
	Bundle *bundle = janet_getpointer(argv, 0);
	script_config(argv[4], argv[3], bundle->config);
	if (bundle->info)
		script_info(argv[4], argv[5], bundle->info);
	*bundle->binary = copy_string(janet_getcstring(argv, 1));
	if (!*bundle->binary)
		janet_panic("out of memory");
	return janet_wrap_nil();
}

int read_bundle(const char *path, char **binary, PluginConfig *config, PluginInfo *info) {
	*binary = NULL;
	*config = (PluginConfig){0};
	if (info)
		*info = (PluginInfo){0};
	JanetTable *env = script_env(DEFAULT_SAMPLE_RATE);
	if (!env)
		return -1;
	Bundle bundle = {binary, config, info};
	janet_def(env, "host/config", janet_wrap_cfunction(config_native), NULL);
	janet_def(env, "host/destination", janet_wrap_pointer(&bundle), NULL);
	janet_def(env, "host/bundle", janet_cstringv(path), NULL);
	int result = janet_dostring(env,
	    "(def p (plugin/read host/bundle)) (host/config host/destination (p :binary) (p :layout) (p :defaults) p (string (json/encode (p :product))))",
	    path, NULL);
	janet_deinit();
	return result ? -1 : 0;
}

int open_bundle(Plugin *e, const char *path, unsigned sample_rate) {
	char *binary;
	PluginConfig config = {0};
	if (read_bundle(path, &binary, &config, NULL)) {
		plugin_config_free(&config);
		free(binary);
		return -1;
	}
	int result = open_plugin(e, binary, &config, sample_rate);
	free(binary);
	plugin_config_free(&config);
	return result;
}
