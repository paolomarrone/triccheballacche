#include "posix/module.h"
#include "script.h"
#include "support.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int plugin(Score *s, int effect, float gain) {
	char *binary;
	PluginConfig config;
	assert(!read_bundle(effect ? "build/test/effect.perone" : "build/test/fixture.perone", &binary, &config, NULL));
	config.defaults[1] = gain;
	int id = score_plugin(s, binary, &config);
	free(binary);
	assert(id >= 0);
	return id;
}

static int mix(Score *s, int a, int b) {
	int inputs[] = {a, b};
	int id = score_mix(s, inputs, 2);
	assert(id >= 0);
	return id;
}

static void graph(Session *s) {
	Score score = {.sample_rate = s->sample_rate};
	int fx = plugin(&score, 1, .5f), source = plugin(&score, 0, .25f);
	assert(score_through(&score, source, fx) == fx); // Declaration order is not processing order.
	int dry = score_mix(&score, &source, 1), wet = score_mix(&score, &fx, 1);
	assert(dry >= 0 && wet >= 0);
	assert(!score_set(&score, wet, 0, 0) && !score_set(&score, fx, 2, .5f));
	assert(!score_param(&score, source, 7, 1, .75f));
	assert(!score_param(&score, dry, 13, 0, .25f) && !score_param(&score, wet, 13, 0, .75f));
	assert(score_output(&score, mix(&score, dry, wet)) >= 0);
	assert(!start_score(s, &score, 1025));
}

static void test_shared(void) {
	Session a = {.sample_rate = 48000}, b = {.sample_rate = 48000};
	graph(&a);
	graph(&b);
	float whole[2050], split[2050];
	assert(!session_render(&a, whole, 1025));
	for (int i = 0; i < 1025; ++i)
		assert(!session_render(&b, split + 2 * i, 1));
	assert(!memcmp(whole, split, sizeof(whole)));
	float state = 0;
	for (int i = 0; i < 1025; ++i) {
		float input = i < 7 ? .25f : .75f;
		state += .5f * (input - state);
		float expected = i < 13 ? input : .25f * input + .75f * .5f * (input - state);
		assert(fabsf(whole[2 * i] - expected) < 1e-7f && whole[2 * i + 1] == -whole[2 * i]);
	}
	assert(!session_seek(&a, 0) && !session_render(&a, split, 1025));
	assert(!memcmp(whole, split, sizeof(whole)));
	session_free(&a);
	session_free(&b);

	Score score = {0};
	int source = plugin(&score, 0, .25f);
	assert(score_output(&score, mix(&score, source, source)) >= 0);
	assert(!start_score(&a, &score, 1) && !session_render(&a, whole, 1));
	assert(whole[0] == .5f && whole[1] == -.5f);
	session_free(&a);
	puts("OK: shared DSP rendered once, parallel state, sample-accurate crossfade, block invariance and rewind");
}

static void level(Session *s, float expected) {
	float audio[1024];
	assert(!session_render(s, audio, 512));
	assert(fabsf(audio[1022] - expected) < 1e-6f && audio[1023] == -audio[1022]);
}

static void test_solo(void) {
	Session s = {.sample_rate = 48000};
	Score score = {.sample_rate = 48000};
	int a = score_track(&score, plugin(&score, 0, .25f), NULL, 0, 0);
	int b = score_track(&score, plugin(&score, 0, .5f), NULL, 0, 0);
	int bus = score_track(&score, mix(&score, a, b), NULL, 0, 0);
	int fx = plugin(&score, 1, .5f);
	int wet = score_track(&score, bus, &fx, 1, 0);
	assert(a >= 0 && b >= 0 && bus >= 0 && wet >= 0);
	// A shared bus feeds a direct branch with no track and a parallel effect track.
	assert(score_output(&score, mix(&score, bus, wet)) >= 0 && !start_score(&s, &score, 10000));
	level(&s, 1.125f);
	assert(!session_listen(&s, 3, TRACK_SOLO));
	level(&s, .375f); // Solo wet retains both sources, but must exclude the shared bus's direct output.
	assert(!session_listen(&s, 0, TRACK_MUTE));
	level(&s, .25f);
	assert(!session_listen(&s, 0, TRACK_SOLO) && !session_listen(&s, 3, 0));
	level(&s, .375f); // Solo source retains both downstream branches, excluding its sibling.
	assert(!session_listen(&s, 0, 0) && !session_listen(&s, 2, TRACK_SOLO));
	level(&s, 1.125f);
	assert(!session_listen(&s, 2, TRACK_MUTE | TRACK_SOLO));
	level(&s, 0);
	assert(!session_seek(&s, 0));
	level(&s, 0);
	assert(!session_listen(&s, 2, 0));
	level(&s, 1.125f);
	session_free(&s);
	puts("OK: nested bus and source solo, shared dry/wet branches, mute precedence and restart");
}

static void test_invalid(void) {
	Score s = {0};
	int source = plugin(&s, 0, .25f), fx = plugin(&s, 1, 1);
	assert(score_end(&s, 1) < 0 && strstr(s.error, "output"));
	assert(score_mix(&s, &source, 0) < 0 && score_output(&s, -1) < 0);
	assert(score_through(&s, source, source) < 0);
	assert(score_output(&s, fx) >= 0 && score_output(&s, source) < 0);
	assert(score_end(&s, 1) < 0 && strstr(s.error, "input"));
	assert(score_through(&s, fx, fx) >= 0);
	assert(score_end(&s, 1) < 0 && strstr(s.error, "cycle"));
	assert(!s.sealed);
	score_free(&s);
	source = plugin(&s, 0, .25f);
	fx = plugin(&s, 1, 1);
	assert(score_through(&s, source, fx) >= 0 && score_through(&s, source, fx) < 0);
	assert(score_output(&s, source) >= 0);
	assert(score_end(&s, 1) < 0 && strstr(s.error, "reach"));
	score_free(&s);
	source = plugin(&s, 0, .25f);
	int chain[] = {plugin(&s, 1, 1), plugin(&s, 1, 1)};
	assert(score_through(&s, source, chain[1]) >= 0);
	assert(score_track(&s, source, chain, 2, 0) < 0);
	assert(!s.ntracks && s.nnodes == 3 && !s.nodes[chain[0]].ninputs);
	assert(score_through(&s, source, chain[0]) >= 0);
	assert(score_output(&s, mix(&s, chain[0], chain[1])) >= 0 && !score_end(&s, 1));
	Session session = {0};
	assert(!session_activate(&session, &s));
	float audio[2];
	assert(!session_render(&session, audio, 1) && audio[0] == .5f && audio[1] == -.5f);
	session_free(&session);
	score_free(&s);
	puts("OK: missing/repeated output, orphan nodes, unbound/rebound effects and cycles rejected before playback");
}

int main(void) {
	test_shared();
	test_solo();
	test_invalid();
	return 0;
}
