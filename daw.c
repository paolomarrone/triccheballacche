#include "daw.h"
#include "script.h"
#include "util.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Only the synchronous Janet adapter has a current context; the C engine does not.
static Session *current;
static Output *output;
static ScoreView *projection;

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

static Janet event_count(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 1);
	return janet_wrap_number(current->nodes[handle(argv[0])].count);
}

// Copy optional script annotations while Janet is alive. Event identity and timing come from Session.
static Janet project(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 2);
	ScoreView *view = projection;
	if (!view || view->nnodes || !current->sealed)
		janet_panic("Score view requires a sealed session and an empty destination");
	if (score_view_init(view, current))
		janet_panic("Cannot allocate score view");
	for (int i = 0; i < view->nnodes; ++i) {
		Janet product = janet_get(argv[1], janet_wrap_integer(i));
		if (janet_checktype(product, JANET_NIL))
			continue;
		Janet name = janet_get(product, janet_ckeywordv("name"));
		if (janet_checktype(name, JANET_STRING)) {
			char *copy = copy_string(janet_getcstring(&name, 0));
			if (!copy)
				janet_panic("Cannot copy plugin name");
			free(view->nodes[i].name);
			view->nodes[i].name = copy;
		}
	}
	Janet locations = janet_get(argv[0], janet_ckeywordv("locations"));
	JanetView origins = janet_getindexed(&locations, 0);
	view->origins = calloc(origins.len, sizeof(ScoreOrigin));
	if (origins.len && !view->origins)
		janet_panic("Cannot copy source origins");
	view->norigins = origins.len;
	for (int i = 0; i < origins.len; ++i) {
		JanetView frames = janet_getindexed(origins.items + i, 0);
		ScoreOrigin *origin = view->origins + i;
		origin->frames = calloc(frames.len, sizeof(ScoreFrame));
		if (frames.len && !origin->frames)
			janet_panic("Cannot copy source frames");
		origin->count = frames.len;
		for (int j = 0; j < frames.len; ++j) {
			Janet f = frames.items[j], file = janet_get(f, janet_ckeywordv("file"));
			Janet line = janet_get(f, janet_ckeywordv("line"));
			Janet column = option(f, "column", janet_wrap_integer(1));
			int row = janet_getinteger(&line, 0), col = janet_getinteger(&column, 0);
			origin->frames[j] =
			    (ScoreFrame){.file = copy_string(janet_getcstring(&file, 0)), .line = row, .column = col};
			if (!origin->frames[j].file)
				janet_panic("Cannot copy source path");
		}
	}
	Janet events = janet_get(argv[0], janet_ckeywordv("events"));
	JanetView emitted = janet_getindexed(&events, 0);
	for (int i = 0; i < emitted.len; ++i) {
		Janet ids = janet_getindex(emitted.items[i], 2);
		size_t count = janet_getindexed(&ids, 0).len;
		if (count > SIZE_MAX - view->nreferences)
			janet_panic("Too many source references");
		view->nreferences += count;
	}
	view->references = calloc(view->nreferences, sizeof(size_t));
	if (view->nreferences && !view->references)
		janet_panic("Cannot copy source references");
	size_t offset = 0;
	for (int i = 0; i < emitted.len; ++i) {
		Janet record = emitted.items[i], ids = janet_getindex(record, 2);
		JanetView refs = janet_getindexed(&ids, 0);
		int id = handle(janet_getindex(record, 4));
		double order = number(janet_getindex(record, 5), 0, view->nodes[id].raw_count);
		if (order >= view->nodes[id].raw_count || order != floor(order))
			janet_panic("Invalid source event order");
		ScoreEvent *event = view->nodes[id].events + (size_t)order;
		event->first_origin = offset;
		event->norigins = refs.len;
		for (int j = 0; j < refs.len; ++j) {
			int origin = janet_getinteger(refs.items + j, 0);
			if (origin < 0 || (size_t)origin >= view->norigins)
				janet_panic("Invalid source origin");
			view->references[offset++] = origin;
		}
	}
	if (score_view_index(view))
		janet_panic("Cannot index score view");
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

int prepare_score(Session *s, Output *cfg, const char *path, const char *source, char **diagnostics, ScoreView *view) {
	if (diagnostics)
		*diagnostics = NULL;
	if (view)
		*view = (ScoreView){0};
	if (s->nnodes || s->sealed || !session_rate(s)) {
		s->error = "score requires an empty session and a valid sample rate";
		return 1;
	}
	const JanetReg api[] = {{"plugin", plugin, "(native/plugin binary layout defaults) -> plugin handle"},
	    {"track", track, "(native/track source &opt {:effects [...] :gain 1 :pan 0}) -> mixer handle"},
	    {"master", master, "(native/master &opt {:effects [...] :gain 1}) -> mixer handle"},
	    {"note", note, "(daw/note plugin seconds duration pitch &opt velocity)"},
	    {"event-count", event_count, "(native/event-count node) -> scheduled event count"}, {"project", project, NULL},
	    {"param", parameter, "(native/param node seconds index value)"},
	    {"end", end, "(daw/end seconds &opt {:format :float :normalize 0}) Seal the score for CLI export."},
	    {NULL, NULL, NULL}};
	current = s;
	output = cfg;
	projection = view;
	*cfg = (Output){0};
	JanetTable *env = script_env();
	if (!env) {
		current = NULL;
		output = NULL;
		projection = NULL;
		return 1;
	}
	JanetBuffer *errors = NULL;
	if (diagnostics) {
		errors = janet_buffer(0);
		janet_setdyn("err", janet_wrap_buffer(errors));
		janet_table_put(env, janet_ckeywordv("err"), janet_wrap_buffer(errors));
	}
	janet_cfuns_prefix(env, "native", api);
	janet_def(env, "daw/script", janet_cstringv(path), NULL);
	static const char daw_source[] =
#include "build/daw.inc"
	    ;
	int result = janet_dostring(env, daw_source, "lib/daw.janet", NULL);
	if (!result && view)
		result = janet_dostring(env,
		    "(import ./lib/trace :as host-trace)"
		    "(def host/trace-report (host-trace/install (curenv) \"<prepare-score>\"))",
		    "<prepare-score>", NULL);
	if (!result) {
		janet_table_put(env, janet_ckeywordv("source"), janet_cstringv(path));
		janet_table_put(env, janet_ckeywordv("current-file"), janet_cstringv(path));
		result = janet_dostring(env, source ? source : "(dofile daw/script :env (curenv))", path, NULL);
	}
	if (!result && !s->sealed) {
		janet_eprintf("Missing (daw/end seconds)\n");
		result = 1;
	}
	if (!result && view)
		result = janet_dostring(env, "(native/project (host/trace-report) daw/products)", "<prepare-score>", NULL);
	if (result && view)
		score_view_free(view);
	if (errors && errors->count)
		*diagnostics = copy_string((const char *)janet_string(errors->data, errors->count));
	janet_deinit();
	current = NULL;
	output = NULL;
	projection = NULL;
	if (!result)
		s->error = NULL;
	return result;
}

int load_score(Session *s, Output *cfg, const char *path) {
	return prepare_score(s, cfg, path, NULL, NULL, NULL);
}
