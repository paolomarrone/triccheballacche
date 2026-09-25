#include "daw.h"
#include "script.h"
#include "util.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Only the synchronous Janet adapter has a current context; the C engine does not.
static _Thread_local Session *current;
static _Thread_local Output *output;
static _Thread_local ScoreView *projection;

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

static void node_key(int id, const char *key) {
	for (int i = 0; i < current->nnodes; ++i)
		if (current->nodes[i].key && !strcmp(current->nodes[i].key, key))
			janet_panic("duplicate node identity");
	if (!(current->nodes[id].key = copy_string(key)))
		janet_panic("out of memory");
}

static Janet identity(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 2);
	int id = handle(argv[0]);
	if (current->sealed || current->nodes[id].key)
		janet_panic("node identity already assigned");
	node_key(id, janet_getcstring(argv, 1));
	return janet_wrap_nil();
}

static void optional_key(int id, Janet opts, const char *fallback) {
	Janet key = option(opts, "id", janet_wrap_nil());
	if (!janet_checktype(key, JANET_NIL))
		node_key(id, (const char *)janet_getkeyword(&key, 0));
	else if (fallback)
		node_key(id, fallback);
}

static Janet make_track(int32_t argc, Janet *argv, int master) {
	janet_arity(argc, 1, 2);
	int source = handle(argv[0]);
	Janet opts = argc == 2 ? argv[1] : janet_wrap_nil();
	const char *const keys[] = {"gain", "effects", "pan", "name", "id", NULL};
	options(opts, keys);
	float gain = number(option(opts, "gain", janet_wrap_number(1)), 0, 4);
	float pan = number(option(opts, "pan", janet_wrap_number(0)), -1, 1);
	Janet name = option(opts, "name", janet_wrap_nil());
	const char *label = janet_checktype(name, JANET_NIL) ? NULL : janet_getcstring(&name, 0);
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
	checked(session_set(current, id, 1, pan));
	const char *source_key = current->nodes[source].key;
	JanetString fallback = source_key ? janet_formatc("track/%s", source_key) : NULL;
	optional_key(id, opts, master ? "master" : (const char *)fallback);
	if (label && !(current->nodes[id].name = copy_string(label)))
		janet_panic("out of memory");
	return janet_wrap_integer(id);
}

static Janet track(int32_t argc, Janet *argv) {
	return make_track(argc, argv, 0);
}

static Janet master(int32_t argc, Janet *argv) {
	return make_track(argc, argv, 1);
}

static Janet through(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 2);
	return janet_wrap_integer(checked(session_through(current, handle(argv[0]), handle(argv[1]))));
}

static Janet mix(int32_t argc, Janet *argv) {
	janet_arity(argc, 1, 2);
	JanetView inputs = janet_getindexed(argv, 0);
	if (inputs.len < 1 || inputs.len > MAX_NODES)
		janet_panic("invalid mix size");
	int ids[MAX_NODES];
	for (int i = 0; i < inputs.len; ++i)
		ids[i] = handle(inputs.items[i]);
	Janet opts = argc == 2 ? argv[1] : janet_wrap_nil();
	const char *const keys[] = {"gain", "pan", "id", NULL};
	options(opts, keys);
	float gain = number(option(opts, "gain", janet_wrap_number(1)), 0, 4);
	float pan = number(option(opts, "pan", janet_wrap_number(0)), -1, 1);
	int id = checked(session_mix(current, ids, inputs.len));
	optional_key(id, opts, NULL);
	checked(session_set(current, id, 0, gain));
	checked(session_set(current, id, 1, pan));
	return janet_wrap_integer(id);
}

static Janet connect_output(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 1);
	return janet_wrap_integer(checked(session_output(current, handle(argv[0]))));
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
	int id = handle(argv[0]);
	size_t count = current->nodes[id].count;
	if (current->sequence)
		for (size_t i = 0; i < current->sequence->count; ++i)
			count += current->sequence->cues[i].node == id;
	return janet_wrap_number(count);
}

// Copy optional script annotations while Janet is alive. Event identity and timing come from Session.
static Janet project(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 4);
	ScoreView *view = projection;
	if (!view || view->nnodes || !current->sealed)
		janet_panic("Score view requires a sealed session and an empty destination");
	if (score_view_init(view, current))
		janet_panic("Cannot allocate score view");
	for (int i = 0; i < view->nnodes; ++i) {
		Janet product = janet_get(argv[1], janet_wrap_integer(i));
		if (janet_checktype(product, JANET_NIL))
			continue;
		ScoreNode *node = view->nodes + i;
		Janet encoded = janet_get(argv[3], janet_wrap_integer(i));
		node->product = copy_string(janet_getcstring(&encoded, 0));
		if (!node->product)
			janet_panic("Cannot copy product metadata");
		Janet parameters = janet_get(argv[2], janet_wrap_integer(i));
		JanetView controls = janet_getindexed(&parameters, 0);
		if (controls.len != current->nodes[i].dsp[0].config.nparams)
			janet_panic("Plugin parameter metadata changed during preparation");
		for (int j = 0; j < controls.len; ++j) {
			Janet p = controls.items[j], low = janet_get(p, janet_ckeywordv("min")),
			      high = janet_get(p, janet_ckeywordv("max"));
			node->minimum[j] = janet_getnumber(&low, 0);
			node->maximum[j] = janet_getnumber(&high, 0);
			if (janet_truthy(janet_get(p, janet_ckeywordv("integer"))))
				node->integers |= UINT64_C(1) << j;
		}
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

static Janet sequence_begin(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 2);
	if (current->sequence || current->sealed)
		janet_panic("score already declared");
	for (int i = 0; i < current->nnodes; ++i)
		if (current->nodes[i].count)
			janet_panic("use one score scheduling API per program");
	Sequence *s = calloc(1, sizeof(*s));
	if (!s)
		janet_panic("out of memory");
	current->sequence = s;
	s->bpm = number(argv[0], 0.001, 100000);
	s->quantum = number(argv[1], 0.001, 100000);
	return janet_wrap_nil();
}

static Janet cue(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 8);
	if (!current->sequence || current->sealed)
		janet_panic("cue requires an open sequence");
	Cue c = {.node = handle(argv[0]),
	    .start = number(argv[1], -1e9, 1e9),
	    .end = number(argv[2], -1e9, 1e9),
	    .period = number(argv[3], 0, 1e9),
	    .parameter = janet_getinteger(argv, 4),
	    .stream = janet_getinteger(argv, 7)};
	if (c.stream < 0 || c.stream >= 65536)
		janet_panic("invalid event source");
	if (c.parameter < 0) {
		c.pitch = janet_getinteger(argv, 5);
		c.velocity = janet_getinteger(argv, 6);
		if (c.parameter != -1 || c.pitch < 0 || c.pitch > 127 || c.velocity < 1 || c.velocity > 127 ||
		    current->nodes[c.node].dsp[0].config.midi < 0 || !current->nodes[c.node].path || c.end <= c.start)
			janet_panic("invalid note cue");
	} else {
		Node *n = current->nodes + c.node;
		const PluginConfig *config = &n->dsp[0].config;
		if (c.parameter >= (n->path ? config->nparams : 2) ||
		    (n->path && (config->outputs & (UINT64_C(1) << c.parameter))))
			janet_panic("invalid parameter cue");
		c.value = number(argv[5], -FLT_MAX, FLT_MAX);
		if (!n->path && (c.value < (c.parameter ? -1 : 0) || c.value > (c.parameter ? 1 : 4)))
			janet_panic("invalid mixer parameter");
		if (c.end != c.start)
			janet_panic("parameter must be a point event");
	}
	if (sequence_add(current->sequence, c))
		janet_panic("invalid cue or sequence event limit exceeded");
	return janet_wrap_nil();
}

static Janet seal(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 1);
	if (!current->sequence)
		janet_panic("missing sequence");
	uint64_t frames = janet_checktype(argv[0], JANET_NIL) ? UINT64_MAX : sample(janet_getnumber(argv, 0));
	checked(session_end(current, frames));
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
	const JanetReg api[] = {{"sequence", sequence_begin, NULL}, {"cue", cue, NULL}, {"seal", seal, NULL},
	    {"key", identity, NULL}, {"plugin", plugin, "(native/plugin binary layout defaults) -> plugin handle"},
	    {"track", track, "(native/track source &opt {:effects [...] :gain 1 :pan 0}) -> mixer handle"},
	    {"master", master, "(native/master signal &opt options) -> mixer handle"},
	    {"through", through, "(native/through signal effect) -> effect handle"},
	    {"mix", mix, "(native/mix signals &opt {:gain 1 :pan 0}) -> mixer handle"},
	    {"output", connect_output, "(native/output signal) Select the final audio output."},
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
#include "build/generated/daw.inc"
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
		janet_eprintf("Missing (daw/end seconds) or (daw/score pattern)\n");
		result = 1;
	}
	if (!result && view)
		result = janet_dostring(env,
		    "(native/project (host/trace-report) daw/products daw/nodes "
		    "(tabseq [[id p] :pairs daw/products] id (string (json/encode p))))",
		    "<prepare-score>", NULL);
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
