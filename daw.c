// Janet prepares C-owned scores; the interpreter is gone before rendering.
#define main host_main
#include "main.c"
#undef main
#include "janet.h"
#include <float.h>
#include <limits.h>
#include <math.h>

enum { TRACKS = 16, EVENTS = 2048, DRUMS = 8192, MAX_SECONDS = 600 };
enum { CLEAN, BASS, GUITAR };
enum { KICK, SNARE, HAT, OPEN_HAT, CRASH, TOM_HIGH, TOM_LOW };
typedef struct {
	Engine engine;
	Event score[EVENTS];
	float pan, gain, send;
	int color;
} Track;
typedef struct { size_t time; int type; float strength; } Hit;
static Track tracks[TRACKS];
static Hit hits[DRUMS];
static int ntracks, nhits;
static size_t frames;
static float *mix, *send;
static uint32_t seed = 0x706f6c70;
static const float tau = 6.28318530718f;

static double number(const Janet *args, int i, double lo, double hi) {
	double x = janet_getnumber(args, i);
	if (!isfinite(x) || x < lo || x > hi) janet_panic("number outside allowed range");
	return x;
}
static int integer(const Janet *args, int i, int lo, int hi) {
	int x = janet_getinteger(args, i);
	if (x < lo || x > hi) janet_panic("integer outside allowed range");
	return x;
}
static size_t sample(double seconds) {
	if (!isfinite(seconds) || seconds < 0 || seconds > MAX_SECONDS) janet_panic("invalid time");
	return (size_t)llround(seconds * SAMPLE_RATE);
}
static void editable(void) { if (frames) janet_panic("score already sealed by daw/export"); }
static Track *track(const Janet *args) {
	editable();
	return tracks + integer(args, 0, 0, ntracks - 1);
}
static Janet option(Janet opts, const char *key, Janet fallback) {
	Janet x = janet_checktype(opts, JANET_NIL) ? opts : janet_get(opts, janet_ckeywordv(key));
	return janet_checktype(x, JANET_NIL) ? fallback : x;
}
static int choice(Janet x, const char *const *names, int count) {
	for (int i = 0; i < count; ++i) if (janet_keyeq(x, names[i])) return i;
	janet_panic("unknown color or drum name");
}
static Janet instrument(int32_t argc, Janet *argv) {
	janet_arity(argc, 1, 2); editable();
	const char *path = janet_getcstring(argv, 0);
	Janet opts = argc == 2 ? argv[1] : janet_wrap_nil();
	if (!janet_checktype(opts, JANET_NIL)) janet_getdictionary(&opts, 0);
	Janet values[] = {option(opts, "pan", janet_wrap_number(0)),
		option(opts, "gain", janet_wrap_number(1)), option(opts, "send", janet_wrap_number(0))};
	float pan = number(values, 0, -1, 1), gain = number(values, 1, 0, 4), wet = number(values, 2, 0, 1);
	const char *const colors[] = {"clean", "bass", "guitar"};
	int color = choice(option(opts, "color", janet_ckeywordv("clean")), colors, 3);
	Janet params = option(opts, "params", janet_wrap_nil());
	JanetView patch = {0};
	if (!janet_checktype(params, JANET_NIL)) patch = janet_getindexed(&params, 0);
	if (patch.len % 2) janet_panic("params must contain index/value pairs");
	for (int i = 0; i < patch.len; i += 2) {
		integer(patch.items, i, 0, INT_MAX); number(patch.items, i + 1, -FLT_MAX, FLT_MAX);
	}
	if (ntracks == TRACKS) janet_panic("track limit reached");
	Track *t = tracks + ntracks;
	if (open_engine(&t->engine, path)) {
		close_engine(&t->engine); *t = (Track){0}; janet_panic("cannot open instrument");
	}
	t->engine.events = t->score; t->pan = pan; t->gain = gain; t->send = wet; t->color = color;
	for (int i = 0; i < patch.len; i += 2)
		t->engine.module->set_parameter(t->engine.instance, janet_getinteger(patch.items, i), janet_getnumber(patch.items, i + 1));
	t->engine.module->reset(t->engine.instance);
	return janet_wrap_integer(ntracks++);
}
static Janet note(int32_t argc, Janet *argv) {
	janet_arity(argc, 4, 5);
	Track *t = track(argv);
	double time = number(argv, 1, 0, MAX_SECONDS), length = number(argv, 2, 0, MAX_SECONDS);
	size_t on = sample(time), off = sample(time + length);
	int pitch = integer(argv, 3, 0, 127), velocity = argc == 5 ? integer(argv, 4, 1, 127) : 100;
	if (off <= on) janet_panic("note must last at least one sample");
	if (!t->engine.module->midi_msg_in) janet_panic("instrument has no MIDI input");
	if (t->engine.count > EVENTS - 2) janet_panic("event limit reached");
	t->score[t->engine.count++] = (Event){on, -1, 0, {0x90, pitch, velocity}};
	t->score[t->engine.count++] = (Event){off, -1, 0, {0x80, pitch, 0}};
	return janet_wrap_nil();
}
static Janet parameter(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 4);
	Track *t = track(argv);
	size_t time = sample(janet_getnumber(argv, 1));
	int index = integer(argv, 2, 0, INT_MAX);
	float value = number(argv, 3, -FLT_MAX, FLT_MAX);
	if (t->engine.count == EVENTS) janet_panic("event limit reached");
	t->score[t->engine.count++] = (Event){time, index, value, {0}};
	return janet_wrap_nil();
}
static Janet drum(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 3); editable();
	const char *const names[] = {"kick", "snare", "hat", "open-hat", "crash", "tom-high", "tom-low"};
	int type = choice(argv[0], names, 7);
	size_t time = sample(janet_getnumber(argv, 1));
	float strength = number(argv, 2, 0, 1);
	if (nhits == DRUMS) janet_panic("drum limit reached");
	hits[nhits++] = (Hit){time, type, strength};
	return janet_wrap_nil();
}
static Janet export(int32_t argc, Janet *argv) {
	janet_fixarity(argc, 1); editable();
	size_t end = sample(janet_getnumber(argv, 0));
	if (!end) janet_panic("empty duration");
	for (int tr = 0; tr < ntracks; ++tr)
		for (size_t i = 0; i < tracks[tr].engine.count; ++i) {
			Event *e = tracks[tr].score + i;
			if (e->time > end || (e->time == end && (e->parameter >= 0 || e->midi[0] != 0x80)))
				janet_panic("event outside export duration");
		}
	for (int i = 0; i < nhits; ++i) if (hits[i].time >= end) janet_panic("drum outside export duration");
	frames = end;
	return janet_wrap_nil();
}
static int load_score(const char *path) {
	const JanetReg api[] = {
		{"instrument", instrument, "(daw/instrument plugin &opt options) -> track id"},
		{"note", note, "(daw/note track seconds duration pitch &opt velocity)"},
		{"param", parameter, "(daw/param track seconds index value)"},
		{"drum", drum, "(daw/drum kind seconds strength)"},
		{"export", export, "(daw/export seconds) Seal the score; render after the script succeeds."},
		{NULL, NULL, NULL}
	};
	janet_init();
	JanetTable *env = janet_core_env(NULL);
	janet_cfuns_prefix(env, "daw", api);
	janet_def(env, "daw/script", janet_cstringv(path), NULL);
	int result = janet_dostring(env, "(dofile daw/script :env (curenv))", path, NULL);
	janet_deinit();
	if (!result && !frames) { fputs("Missing (daw/export seconds)\n", stderr); result = 1; }
	return result;
}

// Stable ordering: controls, note off, note on. Last control at a sample wins.
static int after(const Event *a, const Event *b) {
	if (a->time != b->time) return a->time > b->time;
	return (a->parameter >= 0 ? 0 : a->midi[0]) > (b->parameter >= 0 ? 0 : b->midi[0]);
}
static void sort_score(Track *t) {
	for (size_t i = 1; i < t->engine.count; ++i) {
		Event e = t->score[i]; size_t j = i;
		while (j && after(t->score + j - 1, &e)) { t->score[j] = t->score[j - 1]; --j; }
		t->score[j] = e;
	}
}
static float noise(void) {
	seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
	return (seed >> 8) * (2.f / 16777216.f) - 1.f;
}
static void pan_add(float *dst, size_t frame, float x, float pan) {
	float angle = (pan + 1.f) * tau / 8.f;
	dst[2 * frame] += x * cosf(angle); dst[2 * frame + 1] += x * sinf(angle);
}
static void render_drum(Hit hit) {
	const float duration[] = {.42f, .32f, .085f, .38f, 1.6f, .3f, .4f};
	const float pans[] = {0, -.08f, .35f, .4f, -.55f, -.4f, .45f};
	int type = hit.type;
	size_t n = duration[type] * SAMPLE_RATE;
	float low = 0;
	for (size_t i = 0; i < n && hit.time + i < frames; ++i) {
		float t = (float)i / SAMPLE_RATE, white = noise(), x;
		low += .17f * (white - low);
		float high = white - low;
		if (type == KICK) {
			float phase = tau * (47.f * t + 3.1f * (1.f - expf(-t / .026f)));
			x = .86f * sinf(phase) * expf(-t * 12.f) + .14f * high * expf(-t * 230.f);
		} else if (type == SNARE) {
			x = .62f * high * expf(-t * 22.f) + .28f * sinf(tau * 186.f * t) * expf(-t * 27.f)
				+ .12f * sinf(tau * 337.f * t) * expf(-t * 39.f);
		} else if (type == HAT || type == OPEN_HAT || type == CRASH) {
			float metal = sinf(tau * 4177.f * t + 2.f * sinf(tau * 683.f * t))
				+ .5f * sinf(tau * 7319.f * t) + .3f * sinf(tau * 10331.f * t);
			float decay = type == HAT ? 65.f : type == OPEN_HAT ? 12.f : 3.8f;
			x = (.7f * high + .14f * metal) * expf(-t * decay);
		} else {
			float f = type == TOM_HIGH ? 172.f : 109.f;
			x = .8f * sinf(tau * (f * t + .9f * (1 - expf(-t * 35.f)))) * expf(-t * 17.f)
				+ .1f * high * expf(-t * 80.f);
		}
		x *= hit.strength * fminf(1.f, t * 1800.f);
		pan_add(mix, hit.time + i, x, pans[type]);
		if (type != KICK) pan_add(send, hit.time + i, .07f * x, pans[type]);
	}
}
static int write_score(const char *path) {
	mix = calloc(frames * 2, sizeof(float)); send = calloc(frames * 2, sizeof(float));
	if (!mix || !send) return 1;
	for (int i = 0; i < nhits; ++i) render_drum(hits[i]);
	for (int tr = 0; tr < ntracks; ++tr) {
		Track *t = tracks + tr;
		sort_score(t);
		float low = 0, dc = 0;
		for (size_t pos = 0; pos < frames;) {
			float buffer[BLOCK];
			size_t n = frames - pos < BLOCK ? frames - pos : BLOCK;
			render(&t->engine, buffer, NULL, n);
			for (size_t i = 0; i < n; ++i) {
				float x = buffer[i];
				if (t->color == GUITAR) {
					x = tanhf(8 * x); dc += .013f * (x - dc); x -= dc;
					low += .34f * (x - low); x = low;
				} else if (t->color == BASS) x = .7f * tanhf(2 * x);
				x *= t->gain;
				pan_add(mix, pos + i, x, t->pan); pan_add(send, pos + i, x * t->send, t->pan);
			}
			pos += n;
		}
	}
	float peak = 0, dc[2] = {0};
	const int delays[] = {4807, 10275, 16185};
	for (size_t i = 0; i < frames; ++i) {
		float fade = fminf(1.f, (frames - i) / (.65f * SAMPLE_RATE));
		for (int c = 0; c < 2; ++c) {
			float x = mix[2 * i + c];
			for (int d = 0; d < 3; ++d)
				if (i >= (size_t)delays[d]) x += send[2 * (i - delays[d]) + (c ^ (d & 1))] / (d + 1);
			x = tanhf(1.35f * x); dc[c] += .002f * (x - dc[c]);
			x = (x - dc[c]) * fade * fade;
			if (!isfinite(x)) return 1;
			mix[2 * i + c] = x; peak = fmaxf(peak, fabsf(x));
		}
	}
	ma_encoder encoder;
	ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 2, SAMPLE_RATE);
	if (ma_encoder_init_file(path, &cfg, &encoder) != MA_SUCCESS) return 1;
	int result = 0;
	float norm = peak ? .94f * 32767 / peak : 0;
	for (size_t pos = 0; pos < frames;) {
		int16_t pcm[BLOCK * 2];
		size_t n = frames - pos < BLOCK ? frames - pos : BLOCK;
		for (size_t i = 0; i < 2 * n; ++i) {
			float x = mix[2 * pos + i];
			pcm[i] = (int16_t)lrintf(isfinite(norm) ? x * norm : (x / peak) * (.94f * 32767));
		}
		ma_uint64 written;
		if (ma_encoder_write_pcm_frames(&encoder, pcm, n, &written) != MA_SUCCESS || written != n) { result = 1; break; }
		pos += n;
	}
	ma_encoder_uninit(&encoder);
	return result;
}
static void cleanup(void) {
	for (int tr = 0; tr < ntracks; ++tr) close_engine(&tracks[tr].engine);
	free(mix); free(send);
}
#ifndef DAW_TEST
int main(int argc, char **argv) {
	if (argc != 3) { fprintf(stderr, "Usage: %s score.janet output.wav\n", argv[0]); return 1; }
	int result = load_score(argv[1]) || write_score(argv[2]);
	if (result) fputs("Score/render failed\n", stderr);
	else printf("%.3f seconds, stereo, %s\n", (double)frames / SAMPLE_RATE, argv[2]);
	cleanup();
	return result;
}
#endif
