#include "script.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void janet_json(JanetTable *env);
static const char perone_source[] =
#include "build/perone.inc"
;
JanetTable *script_env(void) {
	janet_init();
	JanetTable *env = janet_core_env(NULL), *json = janet_table(0);
	janet_json(json);
	Janet decode;
	janet_resolve(json, janet_csymbol("decode"), &decode);
	janet_def(env, "json/decode", decode, NULL);
	janet_def(env, "host/platform", janet_cstringv(PERONE_PLATFORM), NULL);
	janet_def(env, "host/max-params", janet_wrap_integer(MAX_PARAMS), NULL);
	janet_def(env, "host/max-inputs", janet_wrap_integer(MAX_INPUTS), NULL);
	if (janet_dostring(env, perone_source, "lib/perone.janet", NULL)) { janet_deinit(); return NULL; }
	return env;
}

void script_config(Janet layout, Janet defaults, PluginConfig *config) {
	JanetView buses = janet_getindexed(&layout, 0), params = janet_getindexed(&defaults, 0);
	if (buses.len != 5 || params.len > MAX_PARAMS) janet_panic("invalid plugin configuration");
	*config = (PluginConfig){.input = janet_getinteger(buses.items, 0), .output = janet_getinteger(buses.items, 1),
		.midi = janet_getinteger(buses.items, 2), .input_offset = janet_getinteger(buses.items, 3),
		.inputs = janet_getinteger(buses.items, 4), .nparams = params.len};
	for (int i = 0; i < params.len; ++i) {
		if (janet_checktype(params.items[i], JANET_NIL)) config->outputs |= UINT64_C(1) << i;
		else {
			config->defaults[i] = janet_getnumber(params.items, i);
			if (!isfinite(config->defaults[i])) janet_panic("invalid plugin default");
		}
	}
}
typedef struct { char **binary; PluginConfig *config; } Bundle;
static Janet config_native(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 4);
	Bundle *bundle = janet_getpointer(argv, 0);
	script_config(argv[2], argv[3], bundle->config);
	*bundle->binary = strdup(janet_getcstring(argv, 1));
	if (!*bundle->binary) janet_panic("out of memory");
	return janet_wrap_nil();
}
int read_bundle(const char *path, char **binary, PluginConfig *config) {
	*binary = NULL;
	JanetTable *env = script_env();
	if (!env) return -1;
	Bundle bundle = {binary, config};
	janet_def(env, "host/config", janet_wrap_cfunction(config_native), NULL);
	janet_def(env, "host/destination", janet_wrap_pointer(&bundle), NULL);
	janet_def(env, "host/bundle", janet_cstringv(path), NULL);
	int result = janet_dostring(env,
		"(def p (perone/read host/bundle)) (host/config host/destination (p :binary) (p :layout) (p :defaults))",
		path, NULL);
	janet_deinit();
	return result ? -1 : 0;
}
int open_bundle(Engine *e, const char *path) {
	char *binary; PluginConfig config;
	if (read_bundle(path, &binary, &config)) return -1;
	int result = open_engine(e, binary, &config);
	free(binary);
	return result;
}
