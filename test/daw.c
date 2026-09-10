#include "daw.h"
#include "script.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int script(Session *s, Output *cfg, const char *source) {
	char path[] = "build/daw-test-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	FILE *f = fdopen(fd, "w");
	assert(f && fputs(source, f) >= 0 && !fclose(f));
	int result = load_score(s, cfg, path);
	assert(!unlink(path));
	return result;
}

static void test_external_metadata(void) {
	Session s = {0};
	Output cfg;
	assert(!script(&s, &cfg,
	    "(def p (daw/plugin \"build/fixture.perone\" {:gain 0.25})) "
	    "(def row ((daw/info p) 1)) (assert (= (row :label) \"Intensità \\\"音\\\"\")) "
	    "(assert (= (get-in row [:scale-points :Full]) 1)) "
	    "(assert (= (row :default) 0.5)) "
	    "(daw/track p) (daw/param p 0.0001 :gain 0.75) (gccollect) (daw/end 0.01)"));
	float out[20];
	assert(!session_render(&s, out, 10));
	for (int i = 0; i < 10; ++i) {
		assert(out[2 * i] == (i < 4 ? .25f : .75f));
		assert(out[2 * i + 1] == -out[2 * i]);
	}
	session_free(&s);
	puts("OK: external Unicode metadata, scale points, output-first indices and Janet lifetime");
}

static void bad_script(const char *source) {
	Session s = {0};
	Output cfg;
	assert(script(&s, &cfg, source));
	session_free(&s);
}

static void noop(void *p) {
	(void)p;
}

static void set(void *p, size_t i, float v) {
	(void)i;
	*(float *)p = v;
}

static void constant(void *p, const float **in, float **out, size_t n) {
	(void)in;
	for (size_t i = 0; i < n; ++i)
		out[0][i] = *(float *)p;
}

static void multiply(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i)
		out[0][i] = in[0][i] * *(float *)p;
}

static void add(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i)
		out[0][i] = in[0][i] + *(float *)p;
}

static void stereo_source(void *p, const float **in, float **out, size_t n) {
	assert(!in);
	for (size_t i = 0; i < n; ++i) {
		out[0][i] = *(float *)p;
		out[1][i] = -.5f * *(float *)p;
	}
}

static void swap(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		out[0][i] = in[1][i] * *(float *)p;
		out[1][i] = in[0][i] * *(float *)p;
	}
}

static void spread(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		out[0][i] = in[0][i] * *(float *)p;
		out[1][i] = -out[0][i];
	}
}

static void sum(void *p, const float **in, float **out, size_t n) {
	for (size_t i = 0; i < n; ++i)
		out[0][i] = .5f * (in[0][i] + in[1][i]) * *(float *)p;
}

static int mock(Session *s, int kind, float value) {
	static const perone_api api[] = {
	    {.free = free, .fini = noop, .reset = noop, .set_parameter = set, .process = constant},
	    {.free = free, .fini = noop, .reset = noop, .set_parameter = set, .process = multiply},
	    {.free = free, .fini = noop, .reset = noop, .set_parameter = set, .process = add},
	    {.free = free, .fini = noop, .reset = noop, .set_parameter = set, .process = stereo_source},
	    {.free = free, .fini = noop, .reset = noop, .set_parameter = set, .process = swap},
	    {.free = free, .fini = noop, .reset = noop, .set_parameter = set, .process = spread},
	    {.free = free, .fini = noop, .reset = noop, .set_parameter = set, .process = sum}};
	static const int inputs[] = {0, 1, 1, 0, 2, 1, 2}, outputs[] = {1, 1, 1, 2, 2, 2, 1};
	Node *n = s->nodes + s->nnodes;
	n->path = strdup("test");
	Engine *e = n->dsp;
	e->module = calloc(1, sizeof(*e->module));
	e->instance = malloc(sizeof(float));
	assert(n->path && e->module && e->instance);
	*e->module = (Module){.api = api + kind,
	    .config = {.input = inputs[kind],
	        .inputs = inputs[kind],
	        .output = outputs[kind],
	        .midi = -1,
	        .nparams = 1,
	        .defaults = {value}}};
	e->initialized = 1;
	return s->nnodes++;
}

static int session_bundle(Session *s, const char *path) {
	char *binary;
	PluginConfig config;
	assert(!read_bundle(path, &binary, &config));
	int id = session_plugin(s, binary, &config);
	free(binary);
	return id;
}

static void test_initial_parameters(void) {
	Session s = {0};
	float out[8];
	int source = mock(&s, 3, 1), fx = session_bundle(&s, "build/effect.perone");
	assert(fx >= 0 && !session_set(&s, fx, 1, .5f)); // Before the second mono instance exists.
	assert(session_track(&s, source, &fx, 1, 0) >= 0);
	assert(!session_param(&s, fx, 2, 2, 0));
	assert(!session_set(&s, fx, 2, .5f)); // Initial values still apply to both instances.
	assert(!session_end(&s, 4) && !session_render(&s, out, 4));
	for (int i = 0; i < 4; ++i) {
		float level = i < 2 ? .5f * powf(.5f, i + 1) : .5f;
		assert(fabsf(out[2 * i] - level) < 1e-6f);
		assert(fabsf(out[2 * i + 1] + .5f * level) < 1e-6f);
	}
	assert(session_set(&s, fx, 1, 1) < 0);
	session_free(&s);
	puts("OK: initial parameters before/after mono duplication, scheduled overrides and sealed session");
}

static void pipeline(Session *s) {
	int source = mock(s, 0, 1), fx[] = {mock(s, 1, 2), mock(s, 2, .25f)};
	int tr = session_track(s, source, fx, 2, 0), master = session_track(s, -1, NULL, 0, 1);
	assert(tr >= 0 && master >= 0);
	assert(!session_set(s, tr, 1, -1));
	assert(!session_param(s, fx[0], 7, 0, .75f));
	assert(!session_param(s, fx[0], 7, 0, .5f)); // Last control wins.
	assert(!session_param(s, tr, 13, 0, .5f));
	assert(!session_param(s, tr, 17, 1, 1));
	assert(!session_param(s, master, 19, 0, .5f));
	assert(!session_end(s, 33));
}

static void test_pipeline(void) {
	Session a = {0}, b = {0};
	float x[66], y[66];
	pipeline(&a);
	pipeline(&b);
	assert(!session_render(&a, x, 33));
	for (size_t i = 0; i < 33; ++i)
		assert(!session_render(&b, y + 2 * i, 1));
	assert(!memcmp(x, y, sizeof(x)));
	for (size_t i = 0; i < 33; ++i) {
		float expected = i < 7 ? 2.25f : i < 13 ? .75f : i < 19 ? .375f : .1875f;
		int c = i < 17 ? 0 : 1;
		assert(fabsf(x[2 * i + c] - expected) < 1e-6f && fabsf(x[2 * i + (c ^ 1)]) < 1e-6f);
	}
	assert(session_render(&a, x, 1) < 0);
	session_free(&a);
	session_free(&b);
	puts("OK: neutral output, serial effects, sample-accurate plugin/gain/pan/master controls, block invariance");
}

static void test_master(void) {
	Session s = {0};
	float audio[64];
	int source = mock(&s, 0, 1), tr = session_track(&s, source, NULL, 0, 0);
	int fx = session_bundle(&s, "build/effect.perone");
	assert(fx >= 0 && !session_set(&s, tr, 1, -1));
	assert(session_track(&s, -1, &fx, 1, 1) >= 0);
	assert(s.nodes[fx].dsp[0].instance != s.nodes[fx].dsp[1].instance);
	assert(!session_set(&s, fx, 2, .5f));
	assert(!session_param(&s, fx, 3, 1, .5f));
	assert(!session_param(&s, fx, 3, 2, 0));
	assert(!session_end(&s, 32) && !session_render(&s, audio, 32));
	for (int i = 0; i < 32; ++i)
		assert(fabsf(audio[2 * i] - (i < 3 ? powf(.5f, i + 1) : .5f)) < 1e-6f && audio[2 * i + 1] == 0);
	session_free(&s);
	puts("OK: master effects use independent stereo state and shared automation");
}

static void stereo_pipeline(Session *s) {
	int source = mock(s, 3, 1), mono = session_bundle(s, "build/effect.perone");
	int fx[] = {mono, mock(s, 4, 1)};
	int tr = session_track(s, source, fx, 2, 0), stereo = mock(s, 4, 1);
	int master = session_track(s, -1, &stereo, 1, 1);
	assert(tr >= 0 && master >= 0);
	assert(s->nodes[mono].dsp[1].instance && !s->nodes[fx[1]].dsp[1].instance && !s->nodes[stereo].dsp[1].instance);
	assert(!session_set(s, mono, 2, .5f));
	assert(!session_param(s, mono, 5, 1, .5f));
	assert(!session_param(s, mono, 5, 2, 0));
	assert(!session_param(s, tr, 7, 0, .5f));
	assert(!session_param(s, tr, 13, 1, -1));
	assert(!session_param(s, tr, 17, 1, 1));
	assert(!session_param(s, master, 19, 0, .25f));
	assert(!session_end(s, 33));
}

static void test_stereo_pipeline(void) {
	Session a = {0}, b = {0};
	float x[66], y[66];
	stereo_pipeline(&a);
	stereo_pipeline(&b);
	assert(!session_render(&a, x, 33));
	for (size_t i = 0; i < 33; ++i)
		assert(!session_render(&b, y + 2 * i, 1));
	assert(!memcmp(x, y, sizeof(x)));
	for (int i = 0; i < 33; ++i) {
		float gain = (i < 7 ? 1 : .5f) * (i < 19 ? 1 : .25f);
		float left = (i < 5 ? powf(.5f, i + 1) : .5f) * gain;
		float right = (i < 5 ? -.5f * powf(.5f, i + 1) : -.25f) * gain;
		if (i >= 13 && i < 17)
			left = 0;
		if (i >= 17)
			right = 0;
		assert(fabsf(x[2 * i] - left) < 1e-6f && fabsf(x[2 * i + 1] - right) < 1e-6f);
	}
	session_free(&a);
	session_free(&b);
	puts("OK: stereo sources, independent mono FX, coupled stereo FX/master, balance and sample-accurate automation");
}

static void test_channel_transitions(void) {
	for (int kind = 0; kind < 3; ++kind) {
		Session s = {0};
		float out[18];
		int source = mock(&s, kind == 2 ? 3 : 0, 1), fx[2], count = 0;
		if (kind == 2)
			fx[count++] = mock(&s, 6, 1); // Explicit stereo-to-mono DSP.
		fx[count++] = mock(&s, kind == 0 ? 4 : 5, 1);
		assert(session_track(&s, source, fx, count, 0) >= 0);
		assert(!session_end(&s, 9) && !session_render(&s, out, 9));
		for (int i = 0; i < 9; ++i) {
			assert(out[2 * i] == (kind == 2 ? .25f : 1));
			assert(out[2 * i + 1] == (kind == 0 ? 1 : kind == 1 ? -1 : -.25f));
		}
		session_free(&s);
	}
	Session s = {0};
	float out[2];
	int source = mock(&s, 3, 1), fx[] = {session_bundle(&s, "build/effect.perone"), mock(&s, 5, 1)};
	assert(session_track(&s, source, fx, 2, 0) < 0); // No implicit stereo fold-down before a mono-to-stereo effect.
	assert(s.ntracks == 0 && !s.nodes[fx[0]].dsp[1].instance && !s.nodes[source].attached);
	assert(session_track(&s, source, fx, 1, 0) >= 0);
	session_free(&s);
	source = mock(&s, 3, 1);
	int downmix = mock(&s, 6, 1);
	assert(session_track(&s, source, NULL, 0, 0) >= 0 && session_track(&s, -1, &downmix, 1, 1) >= 0);
	assert(!session_end(&s, 1) && !session_render(&s, out, 1));
	assert(out[0] == .25f && out[1] == .25f);
	session_free(&s);
	puts("OK: mono/stereo transitions, native mono-to-stereo DSP, explicit downmix and invalid-chain rollback");
}

static void export_session(Session *s, size_t frames, int overflow) {
	int source = mock(s, 3, .25f), track = session_track(s, source, NULL, 0, 0);
	assert(track >= 0 && !session_set(s, track, 0, 4));
	if (overflow)
		assert(!session_param(s, source, BLOCK, 0, FLT_MAX));
	assert(!session_end(s, frames));
}

static void failed_exports(size_t frames, int overflow) {
	for (int pcm16 = 0; pcm16 < 2; ++pcm16)
		for (int normalize = 0; normalize < 2; ++normalize)
			for (int existing = 0; existing < 2; ++existing) {
				char directory[] = "build/export-test-XXXXXX", path[80];
				assert(mkdtemp(directory));
				snprintf(path, sizeof(path), "%s/score.wav", directory);
				const char previous[] = "previous export";
				if (existing) {
					FILE *f = fopen(path, "wb");
					assert(f && fwrite(previous, 1, sizeof(previous), f) == sizeof(previous) && !fclose(f));
				}
				Session s = {0};
				export_session(&s, frames, overflow);
				assert(write_score(&s, &(Output){pcm16, normalize ? .94f : 0}, path) < 0 && s.error);
				if (overflow)
					assert(!strcmp(s.error, "non-finite audio"));
				if (frames == 2)
					assert(s.time == frames); // The error happens after all audio has been rendered.
				session_free(&s);
				if (existing) {
					char actual[sizeof(previous)];
					FILE *f = fopen(path, "rb");
					assert(f && fread(actual, 1, sizeof(actual), f) == sizeof(actual));
					assert(!memcmp(actual, previous, sizeof(actual)) && fgetc(f) == EOF && !fclose(f));
					assert(!unlink(path));
				} else
					assert(access(path, F_OK) != 0);
				assert(!rmdir(directory)); // No temporary export survives an error.
			}
}

static void test_atomic_export(void) {
	failed_exports(BLOCK + 1, 1);
	for (int late = 0; late < 2; ++late) {
		pid_t child = fork();
		assert(child >= 0);
		if (!child) {
			assert(signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
			rlim_t limit = late ? 48 : 128;
			assert(!setrlimit(RLIMIT_FSIZE, &(struct rlimit){limit, limit}));
			failed_exports(late ? 2 : BLOCK + 1, 0);
			_exit(0);
		}
		int status;
		assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}
	char directory[] = "build/export-test-XXXXXX", path[80];
	assert(mkdtemp(directory));
	snprintf(path, sizeof(path), "%s/score.wav", directory);
	assert(!mkdir(path, 0700));
	Session s = {0};
	export_session(&s, 2, 0);
	assert(write_score(&s, &(Output){0}, path) < 0 && s.error);
	session_free(&s);
	assert(!rmdir(path)); // Failed rename preserves the existing directory.
	snprintf(path, sizeof(path), "%s/missing/score.wav", directory);
	export_session(&s, 2, 0);
	assert(write_score(&s, &(Output){0}, path) < 0 && s.time == 0 && s.error);
	session_free(&s);
	assert(!rmdir(directory));
	puts("OK: failed render/write/finalization/rename preserves the destination and removes temporary files");
}

static void test_stereo_wav(void) {
	Session s = {0};
	Output cfg = {0};
	int source = mock(&s, 3, .25f);
	assert(session_track(&s, source, NULL, 0, 0) >= 0);
	assert(!session_end(&s, 1025) && !write_score(&s, &cfg, "build/stereo-test.wav"));
	FILE *f = fopen("build/stereo-test.wav", "rb");
	unsigned char header[44];
	assert(f && fread(header, 1, 44, f) == 44 && header[22] == 2);
	for (int i = 0; i < 1025; ++i) {
		float pair[2];
		assert(fread(pair, sizeof(pair), 1, f) == 1);
		assert(pair[0] == .25f && pair[1] == -.125f);
	}
	assert(fgetc(f) == EOF);
	fclose(f);
	session_free(&s);
	puts("OK: stereo WAV preserves distinct left and right channels at unity center balance");
}

static void test_wav(float value, float gain, Output cfg, float expected) {
	Session s = {0};
	int source = mock(&s, 0, value), tr = session_track(&s, source, NULL, 0, 0);
	assert(!session_set(&s, tr, 0, gain) && !session_set(&s, tr, 1, -1));
	assert(!session_end(&s, 1025) && !write_score(&s, &cfg, "build/export-test.wav"));
	FILE *f = fopen("build/export-test.wav", "rb");
	unsigned char header[44];
	assert(f && fread(header, 1, sizeof(header), f) == sizeof(header));
	assert(!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVEfmt ", 8));
	assert(header[20] == (cfg.pcm16 ? 1 : 3) && header[22] == 2 && header[34] == (cfg.pcm16 ? 16 : 32));
	for (int i = 0; i < 1025; ++i) {
		if (cfg.pcm16) {
			int16_t pcm[2];
			assert(fread(pcm, sizeof(pcm), 1, f) == 1);
			assert(abs(pcm[0] - (int)lrintf(expected * 32767)) <= 1 && pcm[1] == 0);
		} else {
			float pcm[2];
			assert(fread(pcm, sizeof(pcm), 1, f) == 1);
			assert(fabsf(pcm[0] - expected) < 1e-6f && pcm[1] == 0);
		}
	}
	assert(fgetc(f) == EOF);
	fclose(f);
	session_free(&s);
}

int main(void) {
	test_external_metadata();
	test_initial_parameters();
	test_pipeline();
	test_master();
	test_stereo_pipeline();
	test_channel_transitions();
	test_atomic_export();
	test_stereo_wav();
	test_wav(.25f, 1, (Output){0}, .25f);
	test_wav(.25f, .5f, (Output){0}, .125f);
	test_wav(2, 1, (Output){0}, 2); // Float WAV has no hidden clipping.
	test_wav(2, 1, (Output){.pcm16 = 1}, 1);
	test_wav(1e-35f, 1, (Output){.pcm16 = 1, .normalize = .94f}, .94f);
	test_wav(0, 1, (Output){.normalize = .94f}, 0);
	puts("OK: neutral float WAV, proportional gain, optional PCM16/normalization, silence and tiny values");
	Session s = {0};
	Output cfg;
	assert(!load_score(&s, &cfg, "test/daw.janet"));
	assert(s.nnodes == 4 && s.nodes[0].count == 3003 && s.frames == 61 * SAMPLE_RATE);
	float audio[BLOCK * 2];
	double energy = 0;
	while (s.time < s.frames) {
		size_t n = s.frames - s.time < BLOCK ? s.frames - s.time : BLOCK;
		assert(!session_render(&s, audio, n));
		for (size_t i = 0; i < n * 2; ++i)
			energy += audio[i] * audio[i];
	}
	assert(energy > 1 && s.nodes[0].dsp[0].next == 3003);
	session_free(&s);
	puts("OK: metadata, strict options/ranges, ownership, GC, 60 seconds of automation beyond 2048 events");
	bad_script("(");
	bad_script("unknown-binding");
	bad_script("(daw/end 1) (error \"expected failure after end\")");
	bad_script("(+ 1 2)");
	bad_script("(daw/plugin \"build/fixture.perone\") (daw/end 1)");
	puts("OK: parse/runtime errors, missing end and orphan plugins fail cleanly");
	return 0;
}
