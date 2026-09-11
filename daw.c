#include "daw.h"
#include "script.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Only the synchronous Janet adapter has a current context; the C engine does not.
static Session *current;
static Output *output;

static int checked(int result) {
	if (result < 0)
		janet_panic(current->error);
	return result;
}

static double number(Janet x, double lo, double hi) {
	double value = janet_getnumber(&x, 0);
	if (!isfinite(value) || value < lo || value > hi)
		janet_panic("number outside range");
	return value;
}

static size_t sample(double seconds) {
	if (!isfinite(seconds) || seconds < 0 || seconds > 3600)
		janet_panic("time outside 0..3600 seconds");
	return (size_t)llround(seconds * current->sample_rate);
}

static int handle(Janet x) {
	int id = janet_getinteger(&x, 0);
	if (id < 0 || id >= current->nnodes)
		janet_panic("invalid node handle");
	return id;
}

static Janet option(Janet opts, const char *key, Janet fallback) {
	Janet x = janet_checktype(opts, JANET_NIL) ? opts : janet_get(opts, janet_ckeywordv(key));
	return janet_checktype(x, JANET_NIL) ? fallback : x;
}

static void options(Janet opts, const char *const *names) {
	if (janet_checktype(opts, JANET_NIL))
		return;
	janet_getdictionary(&opts, 0);
	for (Janet k = janet_next(opts, janet_wrap_nil()); !janet_checktype(k, JANET_NIL); k = janet_next(opts, k)) {
		int found = 0;
		for (int i = 0; names[i]; ++i)
			found |= janet_keyeq(k, names[i]);
		if (!found)
			janet_panicf("unknown option %v", k);
	}
}

static Janet plugin(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 3);
	PluginConfig config;
	script_config(argv[1], argv[2], &config);
	return janet_wrap_integer(checked(session_plugin(current, janet_getcstring(argv, 0), &config)));
}

static Janet make_track(int32_t argc, Janet *argv, int master) {
	int offset = master ? 0 : 1;
	janet_arity(argc, offset, offset + 1);
	int source = master ? -1 : handle(argv[0]);
	Janet opts = argc > offset ? argv[offset] : janet_wrap_nil();
	const char *const track_keys[] = {"gain", "effects", "pan", NULL}, *const master_keys[] = {"gain", "effects", NULL};
	options(opts, master ? master_keys : track_keys);
	float gain = number(option(opts, "gain", janet_wrap_number(1)), 0, 4);
	float pan = number(option(opts, "pan", janet_wrap_number(0)), -1, 1);
	Janet fx = option(opts, "effects", janet_wrap_nil());
	int ids[MAX_FX], count = 0;
	if (!janet_checktype(fx, JANET_NIL)) {
		JanetView list = janet_getindexed(&fx, 0);
		if (list.len > MAX_FX)
			janet_panic("too many effects");
		for (int i = 0; i < list.len; ++i)
			ids[count++] = handle(list.items[i]);
	}
	int id = checked(session_track(current, source, ids, count, master));
	checked(session_set(current, id, 0, gain));
	if (!master)
		checked(session_set(current, id, 1, pan));
	return janet_wrap_integer(id);
}

static Janet track(int32_t argc, Janet *argv) {
	return make_track(argc, argv, 0);
}

static Janet master(int32_t argc, Janet *argv) {
	return make_track(argc, argv, 1);
}

static Janet note(int32_t argc, Janet *argv) {
	janet_arity(argc, 4, 5);
	int id = handle(argv[0]), pitch = janet_getinteger(argv, 3), velocity = argc == 5 ? janet_getinteger(argv, 4) : 100;
	double time = number(argv[1], 0, 3600), length = number(argv[2], 0, 3600);
	checked(session_note(current, id, sample(time), sample(time + length), pitch, velocity));
	return janet_wrap_nil();
}

static Janet parameter(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 4);
	int id = handle(argv[0]), p = janet_getinteger(argv, 2);
	checked(session_param(current, id, sample(janet_getnumber(argv, 1)), p, number(argv[3], -FLT_MAX, FLT_MAX)));
	return janet_wrap_nil();
}

static Janet end(int32_t argc, Janet *argv) {
	janet_arity(argc, 1, 2);
	Janet opts = argc == 2 ? argv[1] : janet_wrap_nil();
	const char *const names[] = {"format", "normalize", NULL};
	options(opts, names);
	Janet fmt = option(opts, "format", janet_ckeywordv("float"));
	if (!janet_keyeq(fmt, "float") && !janet_keyeq(fmt, "pcm16"))
		janet_panic("format must be :float or :pcm16");
	float normalize = number(option(opts, "normalize", janet_wrap_number(0)), 0, 1);
	checked(session_end(current, sample(janet_getnumber(argv, 0))));
	*output = (Output){janet_keyeq(fmt, "pcm16"), normalize};
	return janet_wrap_nil();
}

int load_score(Session *s, Output *cfg, const char *path) {
	if (s->nnodes || s->sealed || !session_rate(s)) {
		s->error = "score requires an empty session and a valid sample rate";
		return 1;
	}
	const JanetReg api[] = {{"plugin", plugin, "(native/plugin binary layout defaults) -> plugin handle"},
	    {"track", track, "(native/track source &opt {:effects [...] :gain 1 :pan 0}) -> mixer handle"},
	    {"master", master, "(native/master &opt {:effects [...] :gain 1}) -> mixer handle"},
	    {"note", note, "(daw/note plugin seconds duration pitch &opt velocity)"},
	    {"param", parameter, "(native/param node seconds index value)"},
	    {"end", end, "(daw/end seconds &opt {:format :float :normalize 0}) Seal the score for CLI export."},
	    {NULL, NULL, NULL}};
	current = s;
	output = cfg;
	*cfg = (Output){0};
	JanetTable *env = script_env();
	if (!env) {
		current = NULL;
		output = NULL;
		return 1;
	}
	janet_cfuns_prefix(env, "native", api);
	janet_def(env, "daw/script", janet_cstringv(path), NULL);
	static const char daw_source[] =
#include "build/daw.inc"
	    ;
	int result = janet_dostring(env, daw_source, "lib/daw.janet", NULL) ||
	    janet_dostring(env, "(dofile daw/script :env (curenv))", path, NULL);
	janet_deinit();
	current = NULL;
	output = NULL;
	if (!result && !s->sealed) {
		fputs("Missing (daw/end seconds)\n", stderr);
		result = 1;
	}
	if (!result)
		s->error = NULL;
	return result;
}
