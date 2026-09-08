#include "daw.h"
#include "audio.h"
#include "janet.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>

// Only the synchronous Janet adapter has a current context; the C engine does not.
static Session *current;
static Output *output;
static int checked(int result) { if (result < 0) janet_panic(current->error); return result; }
static double number(Janet x, double lo, double hi) {
	double value = janet_getnumber(&x, 0);
	if (!isfinite(value) || value < lo || value > hi) janet_panic("number outside range");
	return value;
}
static size_t sample(double seconds) {
	if (!isfinite(seconds) || seconds < 0 || seconds > 3600) janet_panic("time outside 0..3600 seconds");
	return (size_t)llround(seconds * SAMPLE_RATE);
}
static int handle(Janet x) {
	int id = janet_getinteger(&x, 0);
	if (id < 0 || id >= current->nnodes) janet_panic("invalid node handle");
	return id;
}
static int param_index(Node *n, Janet key) {
	if (janet_checktype(key, JANET_NUMBER)) {
		int i = janet_getinteger(&key, 0);
		return i >= 0 && (size_t)i < n->info->count ? i : -1;
	}
	for (size_t i = 0; i < n->info->count; ++i)
		if (janet_keyeq(key, n->info->parameters[i].name)) return (int)i;
	return -1;
}
static int valid_value(Node *n, int index, double value) {
	if (index < 0) return 0;
	const tibia_parameter *p = n->info->parameters + index;
	return isfinite(value) && value >= p->minimum && value <= p->maximum && (!p->integer || floor(value) == value);
}
static Janet option(Janet opts, const char *key, Janet fallback) {
	Janet x = janet_checktype(opts, JANET_NIL) ? opts : janet_get(opts, janet_ckeywordv(key));
	return janet_checktype(x, JANET_NIL) ? fallback : x;
}
static void options(Janet opts, const char *const *names) {
	if (janet_checktype(opts, JANET_NIL)) return;
	janet_getdictionary(&opts, 0);
	for (Janet k = janet_next(opts, janet_wrap_nil()); !janet_checktype(k, JANET_NIL); k = janet_next(opts, k)) {
		int found = 0;
		for (int i = 0; names[i]; ++i) found |= janet_keyeq(k, names[i]);
		if (!found) janet_panicf("unknown option %v", k);
	}
}
static Janet plugin(int32_t argc, Janet *argv) {
	janet_arity(argc, 1, 2);
	const char *path = janet_getcstring(argv, 0);
	Janet params = argc == 2 ? argv[1] : janet_wrap_nil(), keys[MAX_PARAMS];
	double values[MAX_PARAMS]; int count = 0;
	if (!janet_checktype(params, JANET_NIL)) {
		janet_getdictionary(&params, 0);
		for (Janet k = janet_next(params, janet_wrap_nil()); !janet_checktype(k, JANET_NIL); k = janet_next(params, k)) {
			if (count == MAX_PARAMS) janet_panic("too many initial parameters");
			if (janet_checktype(k, JANET_NUMBER)) janet_getinteger(&k, 0);
			else janet_getkeyword(&k, 0);
			keys[count] = k; values[count++] = number(janet_get(params, k), -FLT_MAX, FLT_MAX);
		}
	}
	int id = checked(session_plugin(current, path));
	for (int i = 0; i < count; ++i) {
		int p = param_index(current->nodes + id, keys[i]);
		if (!valid_value(current->nodes + id, p, values[i])) {
			session_pop(current); janet_panicf("unknown parameter or invalid value: %v", keys[i]);
		}
		checked(session_set(current, id, p, values[i]));
	}
	return janet_wrap_integer(id);
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
		if (list.len > MAX_FX) janet_panic("too many effects");
		for (int i = 0; i < list.len; ++i) ids[count++] = handle(list.items[i]);
	}
	int id = checked(session_track(current, source, ids, count, master));
	checked(session_set(current, id, 0, gain));
	if (!master) checked(session_set(current, id, 1, pan));
	return janet_wrap_integer(id);
}
static Janet track(int32_t argc, Janet *argv) { return make_track(argc, argv, 0); }
static Janet master(int32_t argc, Janet *argv) { return make_track(argc, argv, 1); }
static Janet note(int32_t argc, Janet *argv) {
	janet_arity(argc, 4, 5);
	int id = handle(argv[0]), pitch = janet_getinteger(argv, 3), velocity = argc == 5 ? janet_getinteger(argv, 4) : 100;
	double time = number(argv[1], 0, 3600), length = number(argv[2], 0, 3600);
	checked(session_note(current, id, sample(time), sample(time + length), pitch, velocity));
	return janet_wrap_nil();
}
static Janet parameter(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 4);
	int id = handle(argv[0]), p = param_index(current->nodes + id, argv[2]);
	if (p < 0) janet_panicf("unknown parameter %v", argv[2]);
	double value = janet_getnumber(argv, 3);
	if (!valid_value(current->nodes + id, p, value)) janet_panicf("invalid value for %v", argv[2]);
	checked(session_param(current, id, sample(janet_getnumber(argv, 1)), p, value));
	return janet_wrap_nil();
}
static Janet info(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 1);
	const tibia_info *desc = current->nodes[handle(argv[0])].info;
	JanetArray *result = janet_array((int32_t)desc->count);
	for (size_t i = 0; i < desc->count; ++i) {
		const tibia_parameter *p = desc->parameters + i;
		JanetTable *row = janet_table(6);
		janet_table_put(row, janet_ckeywordv("name"), janet_ckeywordv(p->name));
		janet_table_put(row, janet_ckeywordv("unit"), janet_cstringv(p->unit));
		janet_table_put(row, janet_ckeywordv("min"), janet_wrap_number(p->minimum));
		janet_table_put(row, janet_ckeywordv("max"), janet_wrap_number(p->maximum));
		janet_table_put(row, janet_ckeywordv("default"), janet_wrap_number(p->default_value));
		janet_table_put(row, janet_ckeywordv("integer"), janet_wrap_boolean(p->integer));
		janet_array_push(result, janet_wrap_table(row));
	}
	return janet_wrap_array(result);
}
static Janet end(int32_t argc, Janet *argv) {
	janet_arity(argc, 1, 2);
	Janet opts = argc == 2 ? argv[1] : janet_wrap_nil();
	const char *const names[] = {"format", "normalize", NULL}; options(opts, names);
	Janet fmt = option(opts, "format", janet_ckeywordv("float"));
	if (!janet_keyeq(fmt, "float") && !janet_keyeq(fmt, "pcm16")) janet_panic("format must be :float or :pcm16");
	float normalize = number(option(opts, "normalize", janet_wrap_number(0)), 0, 1);
	checked(session_end(current, sample(janet_getnumber(argv, 0))));
	*output = (Output){janet_keyeq(fmt, "pcm16"), normalize};
	return janet_wrap_nil();
}
int load_score(Session *s, Output *cfg, const char *path) {
	const JanetReg api[] = {
		{"plugin", plugin, "(daw/plugin path &opt parameters) -> plugin handle"},
		{"track", track, "(daw/track source &opt {:effects [...] :gain 1 :pan 0}) -> mixer handle"},
		{"master", master, "(daw/master &opt {:effects [...] :gain 1}) -> mixer handle"},
		{"note", note, "(daw/note plugin seconds duration pitch &opt velocity)"},
		{"param", parameter, "(daw/param node seconds parameter value)"},
		{"info", info, "(daw/info node) -> parameter descriptions"},
		{"end", end, "(daw/end seconds &opt {:format :float :normalize 0}) Seal the score for CLI export."},
		{NULL, NULL, NULL}
	};
	current = s; output = cfg; *cfg = (Output){0};
	janet_init();
	JanetTable *env = janet_core_env(NULL);
	janet_cfuns_prefix(env, "daw", api); janet_def(env, "daw/script", janet_cstringv(path), NULL);
	int result = janet_dostring(env, "(dofile daw/script :env (curenv))", path, NULL);
	janet_deinit(); current = NULL; output = NULL;
	if (!result && !s->sealed) { fputs("Missing (daw/end seconds)\n", stderr); result = 1; }
	if (!result) s->error = NULL;
	return result;
}
int write_score(Session *s, const Output *cfg, const char *path) {
	if (!s->sealed || s->time) { s->error = "export requires a fresh, sealed session"; return -1; }
	// Neutral export streams directly. Optional normalization spools to disk, not RAM.
	FILE *spool = cfg->normalize ? tmpfile() : NULL;
	if (cfg->normalize && !spool) return -1;
	float peak = 0, audio[BLOCK * 2];
	if (spool) {
		while (s->time < s->frames) {
			size_t n = s->frames - s->time < BLOCK ? s->frames - s->time : BLOCK;
			if (session_render(s, audio, n) || fwrite(audio, sizeof(float) * 2, n, spool) != n) { fclose(spool); return -1; }
			for (size_t i = 0; i < n * 2; ++i) peak = fmaxf(peak, fabsf(audio[i]));
		}
		if (fseek(spool, 0, SEEK_SET)) { fclose(spool); return -1; }
	}
	ma_encoder encoder;
	ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, cfg->pcm16 ? ma_format_s16 : ma_format_f32, 2, SAMPLE_RATE);
	if (ma_encoder_init_file(path, &config, &encoder) != MA_SUCCESS) { if (spool) fclose(spool); return -1; }
	int result = 0;
	for (size_t pos = 0; pos < s->frames;) {
		size_t n = s->frames - pos < BLOCK ? s->frames - pos : BLOCK;
		if (spool ? fread(audio, sizeof(float) * 2, n, spool) != n : session_render(s, audio, n)) { result = -1; break; }
		if (spool && peak) for (size_t i = 0; i < 2 * n; ++i) audio[i] = (audio[i] / peak) * cfg->normalize;
		int16_t pcm[BLOCK * 2];
		if (cfg->pcm16) for (size_t i = 0; i < 2 * n; ++i) pcm[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, audio[i])) * 32767);
		ma_uint64 written;
		if (ma_encoder_write_pcm_frames(&encoder, cfg->pcm16 ? (const void *)pcm : audio, n, &written) != MA_SUCCESS || written != n) { result = -1; break; }
		pos += n;
	}
	ma_encoder_uninit(&encoder); if (spool) fclose(spool);
	return result;
}
#ifndef DAW_TEST
int main(int argc, char **argv) {
	if (argc != 3) { fprintf(stderr, "Usage: %s score.janet output.wav\n", argv[0]); return 1; }
	Session session = {0}; Output cfg;
	int result = load_score(&session, &cfg, argv[1]) || write_score(&session, &cfg, argv[2]);
	if (result) fprintf(stderr, "Score/render failed%s%s\n", session.error ? ": " : "", session.error ? session.error : "");
	else printf("%.3f seconds, stereo, %s\n", (double)session.frames / SAMPLE_RATE, argv[2]);
	session_free(&session);
	return result;
}
#endif
