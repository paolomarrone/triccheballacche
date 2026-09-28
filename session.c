#include "session.h"
#include <bw_balance.h>
#include <bw_buf.h>
#include <bw_pan.h>
#include <bw_slew_lim.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int fail(Session *s, const char *message) {
	s->error = message;
	return -1;
}

unsigned session_rate(Session *s) {
	return s->sample_rate ? s->sample_rate : DEFAULT_SAMPLE_RATE;
}

static void revision_free(Revision *r) {
	if (!r)
		return;
	sequencer_free(&r->cursor);
	score_free(&r->score);
	free(r);
}

void session_collect(Session *s) {
	revision_free(atomic_exchange(&s->retired, NULL));
}

void session_cancel(Session *s) {
	revision_free(atomic_exchange(&s->pending, NULL));
	session_collect(s);
}

int session_listen(Session *s, int track, int flags) {
	if (!s->audio || track < 0 || track >= s->ntracks || flags < 0 || flags > (TRACK_MUTE | TRACK_SOLO))
		return -1;
	atomic_store_explicit(&s->tracks[track].listen, flags, memory_order_relaxed);
	return 0;
}

static void set_control(Session *s, int id, int parameter, float value) {
	NodeState *n = s->nodes + id;
	if (!n->dsp[0].dsp)
		n->values[parameter] = value;
	else
		for (int c = 0; c < 2 && n->dsp[c].dsp; ++c)
			set_dsp(n->dsp[c].dsp, parameter, value);
}

static void note_message(NodeState *n, int pitch, int velocity) {
	uint8_t message[] = {velocity ? 0x90 : 0x80, pitch, velocity};
	midi_dsp(n->dsp[0].dsp, n->dsp[0].config.midi, message);
}

uint64_t session_frame(const Session *s, double seconds) {
	double value = seconds * s->sample_rate;
	if (!s->audio || !isfinite(value) || value < 0 || value >= 0x1p53)
		return UINT64_MAX;
	uint64_t frame = llround(value);
	return frame <= s->frames ? frame : UINT64_MAX;
}

typedef struct {
	size_t order;
	int pitch, velocity; // Pitch + 1; zero means untouched since reset.
} HeldNote;

static int note_order(const void *aa, const void *bb) {
	const HeldNote *a = aa, *b = bb;
	if (!!a->velocity != !!b->velocity)
		return a->velocity ? 1 : -1;
	return (a->order > b->order) - (a->order < b->order);
}

int session_seek(Session *s, uint64_t from) {
	if (!s->audio)
		return fail(s, "seek requires a prepared session");
	if (from > s->frames || from >= (UINT64_C(1) << 53))
		return fail(s, "position outside score");
	HeldNote(*notes)[128] = from ? calloc(s->nnodes, sizeof(*notes)) : NULL;
	if (from && !notes)
		return fail(s, "out of memory");
	float values[MAX_NODES][MAX_PARAMS];
	Revision *r = s->active;
	const Score *score = &r->score;
	Sequencer *cursor = &r->cursor;
	session_cancel(s);
	r->at = 0;
	memset(s->held, 0, s->nnodes * sizeof(*s->held));
	s->next_off = UINT64_MAX;
	unsigned solo = 0;
	for (int i = 0; i < s->ntracks; ++i) {
		int flags = atomic_load(&s->tracks[i].listen);
		s->tracks[i].level = !(flags & TRACK_MUTE);
		if (flags & TRACK_SOLO)
			solo |= 1u << i;
	}
	for (int i = 0; i < s->nnodes; ++i) {
		const Node *n = score->nodes + i;
		memcpy(values[i], n->config.defaults, n->config.nparams * sizeof(float));
		for (int j = 0; j < n->ninputs; ++j)
			s->nodes[i].inputs[j] = !solo || ((score->nodes[n->inputs[j]].upstream | n->downstream) & solo);
	}
	sequencer_history(cursor, from);
	for (size_t order = 0; cursor->used; ++order) {
		uint64_t end;
		const Cue *cue = sequencer_peek(cursor, &end);
		if (cue->parameter >= 0)
			values[cue->node][cue->parameter] = cue->value;
		else {
			int active = end > from;
			notes[cue->node][cue->pitch] = (HeldNote){order, cue->pitch + 1, active ? cue->velocity : 0};
			s->held[cue->node][cue->pitch] = active ? end : 0;
		}
		sequencer_shift(cursor);
	}
	sequencer_seek(cursor, from);
	for (int i = 0; i < s->nnodes; ++i) {
		const Node *n = score->nodes + i;
		NodeState *state = s->nodes + i;
		for (int pitch = 0; pitch < 128; ++pitch)
			if (s->held[i][pitch] && s->held[i][pitch] < s->next_off)
				s->next_off = s->held[i][pitch];
		for (int j = 0; j < n->config.nparams; ++j)
			if (!(n->config.outputs & (UINT64_C(1) << j)))
				set_control(s, i, j, values[i][j]);
		// Reset after restoring parameters; history and queued UI gestures belong to the old position.
		for (int c = 0; c < 2 && state->dsp[c].dsp; ++c)
			reset_dsp(state->dsp[c].dsp);
		if (notes && n->path && n->config.midi >= 0) {
			qsort(notes[i], 128, sizeof(HeldNote), note_order);
			for (int j = 0; j < 128; ++j)
				if (notes[i][j].pitch)
					note_message(state, notes[i][j].pitch - 1, notes[i][j].velocity);
		}
	}
	free(notes);
	s->time = from;
	s->error = NULL;
	return 0;
}

void session_sync(Session *s) {
	for (int i = 0; i < s->nnodes; ++i)
		sync_dsp(s->nodes[i].dsp[0].dsp, s->nodes[i].dsp[1].dsp);
}

static void fade(
    const bw_slew_lim_coeffs *coeffs, float *level, float target, float *audio, int channels, size_t frames) {
	bw_slew_lim_state state;
	bw_slew_lim_reset_state(coeffs, &state, *level);
	for (size_t i = 0; i < frames; ++i) {
		*level = bw_slew_lim_process1(coeffs, &state, target);
		for (int c = 0; c < channels; ++c)
			audio[i * channels + c] *= *level;
	}
}

static void input_audio(Session *s, const Score *score, int id, int index, unsigned solo,
    const bw_slew_lim_coeffs *slew, float *audio, size_t frames) {
	const Node *node = score->nodes + id;
	int source_id = node->inputs[index];
	const Node *source = score->nodes + source_id;
	const float *input = s->audio + source_id * BLOCK * 2;
	float *level = s->nodes[id].inputs + index;
	float target = !solo || ((source->upstream | node->downstream) & solo);
	if (!s->time)
		*level = target;
	bw_buf_copy(input, audio, frames * source->channels);
	fade(slew, level, target, audio, source->channels, frames);
}

static int process(
    Session *s, const Score *score, int id, unsigned solo, const bw_slew_lim_coeffs *slew, float *out, size_t frames) {
	const Node *node = score->nodes + id;
	NodeState *state = s->nodes + id;
	float input[BLOCK * 2];
	if (!node->ninputs) {
		render_plugin(state->dsp, out, NULL, frames);
		return 0;
	}
	if (!node->path) {
		bw_pan_coeffs panner;
		bw_pan_init(&panner);
		bw_pan_set_sample_rate(&panner, s->sample_rate);
		bw_pan_set_pan(&panner, state->values[1]);
		bw_pan_reset_coeffs(&panner);
		bw_balance_coeffs balance;
		bw_balance_init(&balance);
		bw_balance_set_sample_rate(&balance, s->sample_rate);
		bw_balance_set_balance(&balance, state->values[1]);
		bw_balance_reset_coeffs(&balance);
		float left, right;
		bw_balance_process1(&balance, 1, 1, &left, &right);
		bw_buf_fill(0, out, 2 * frames);
		// The scheduler has already split at every control change.
		for (int j = 0; j < node->ninputs; ++j) {
			input_audio(s, score, id, j, solo, slew, input, frames);
			int channels = score->nodes[node->inputs[j]].channels;
			bw_buf_scale(input, state->values[0], input, channels * frames);
			if (!bw_has_only_finite(input, channels * frames))
				return fail(s, "non-finite audio");
			if (channels == 1) {
				for (size_t k = frames; k-- > 0;) {
					float x = input[k];
					bw_pan_process1(&panner, x, input + 2 * k, input + 2 * k + 1);
				}
			} else {
				for (size_t k = 0; k < frames; ++k) {
					input[2 * k] *= left;
					input[2 * k + 1] *= right;
				}
			}
			bw_buf_mix(out, input, out, 2 * frames);
		}
		return 0;
	}
	input_audio(s, score, id, 0, solo, slew, input, frames);
	if (state->dsp[1].dsp) {
		float mono[BLOCK], tmp[BLOCK];
		for (int c = 0; c < 2; ++c) {
			for (size_t i = 0; i < frames; ++i)
				mono[i] = input[2 * i + c];
			render_plugin(state->dsp + c, tmp, mono, frames);
			for (size_t i = 0; i < frames; ++i)
				out[2 * i + c] = tmp[i];
		}
	} else {
		if (score->nodes[node->inputs[0]].channels == 1 && node->config.input == 2)
			for (size_t i = frames; i-- > 0;)
				input[2 * i] = input[2 * i + 1] = input[i];
		render_plugin(state->dsp, out, input, frames);
	}
	return 0;
}

static int render_audio(Session *s, const Score *score, const bw_slew_lim_coeffs *slew, float *out, size_t frames) {
	unsigned solo = 0, mute = 0;
	for (int i = 0; i < s->ntracks; ++i) {
		int flags = atomic_load_explicit(&s->tracks[i].listen, memory_order_relaxed);
		if (flags & TRACK_SOLO)
			solo |= 1u << i;
		if (flags & TRACK_MUTE)
			mute |= 1u << i;
	}
	for (int i = 0; i < score->norder; ++i) {
		int id = score->order[i];
		const Node *node = score->nodes + id;
		float *audio = s->audio + id * BLOCK * 2;
		if (process(s, score, id, solo, slew, audio, frames))
			return -1;
		if (node->track) {
			int j = node->track - 1;
			TrackState *track = s->tracks + j;
			float target = !(mute & (1u << j));
			if (!s->time)
				track->level = target;
			fade(slew, &track->level, target, audio, 2, frames);
		}
		for (size_t j = 0; j < frames * node->channels; ++j)
			if (!isfinite(audio[j]))
				return fail(s, "non-finite audio");
	}
	const float *audio = s->audio + score->output * BLOCK * 2;
	if (score->nodes[score->output].channels == 1)
		for (size_t i = 0; i < frames; ++i)
			out[2 * i] = out[2 * i + 1] = audio[i];
	else
		memcpy(out, audio, 2 * frames * sizeof(float));
	s->time += frames;
	return 0;
}

void session_free(Session *s) {
	session_cancel(s);
	revision_free(s->active);
	for (int i = 0; i < s->nnodes; ++i) {
		close_plugin(s->nodes[i].dsp);
		close_plugin(s->nodes[i].dsp + 1);
		free(s->nodes[i].inputs);
	}
	free(s->held);
	free(s->audio);
	*s = (Session){0};
}

int session_activate(Session *s, Score *score) {
	if (s->active || s->audio || !score->sealed)
		return fail(s, "activation requires an empty session and a prepared score");
	Revision *r = calloc(1, sizeof(*r));
	if (!r)
		return fail(s, "out of memory");
	r->score = *score;
	Session next = {.modules = s->modules,
	    .sample_rate = score->sample_rate,
	    .frames = score->frames,
	    .nnodes = score->nnodes,
	    .ntracks = score->ntracks};
	const char *error = "out of memory";
	if (sequencer_init(&r->cursor, &r->score.sequence, score->sample_rate, 0))
		goto fail;
	next.audio = calloc(score->nnodes, BLOCK * 2 * sizeof(float));
	next.held = calloc(score->nnodes, sizeof(*next.held));
	if (!next.audio || !next.held)
		goto fail;
	for (int i = 0; i < score->nnodes; ++i) {
		const Node *n = score->nodes + i;
		NodeState *state = next.nodes + i;
		if (n->ninputs && !(state->inputs = calloc(n->ninputs, sizeof(float))))
			goto fail;
		if (!n->path)
			continue;
		int count = n->channels == 2 && n->config.output == 1 ? 2 : 1;
		for (int c = 0; c < count; ++c) {
			state->dsp[c].modules = s->modules;
			if (open_plugin(state->dsp + c, n->path, &n->config, score->sample_rate)) {
				error = "cannot open plugin";
				goto fail;
			}
		}
	}
	next.active = r;
	*score = (Score){0};
	*s = next;
	return session_seek(s, 0);
fail:
	session_free(&next);
	sequencer_free(&r->cursor);
	free(r); // The caller still owns the score.
	return fail(s, error);
}

static int same(const char *a, const char *b) {
	return (!a && !b) || (a && b && !strcmp(a, b));
}

static int same_config(const PluginConfig *a, const PluginConfig *b) {
	return a->input == b->input && a->output == b->output && a->midi == b->midi && a->input_offset == b->input_offset &&
	    a->inputs == b->inputs && a->nparams == b->nparams && a->outputs == b->outputs && a->to_ui == b->to_ui &&
	    a->to_dsp == b->to_dsp;
}

int session_update(Session *s, Score *next, uint64_t earliest, unsigned revision, int *mapping) {
	const char *error = "live update requires the same graph, duration and tempo, with no pending revision";
	if (atomic_load(&s->pending))
		goto fail;
	session_collect(s);
	const Score *active = session_score(s);
	if (!active || !active->live || !next->live || !next->sealed || s->sample_rate != next->sample_rate ||
	    s->frames != next->frames || active->has_master != next->has_master || s->nnodes != next->nnodes ||
	    s->ntracks != next->ntracks || active->sequence.bpm != next->sequence.bpm)
		goto fail;
	int map[MAX_NODES];
	unsigned char used[MAX_NODES] = {0};
	for (int i = 0; i < next->nnodes; ++i) {
		const Node *a = next->nodes + i;
		map[i] = -1;
		if (a->key)
			for (int j = 0; j < s->nnodes; ++j)
				if (same(a->key, active->nodes[j].key))
					map[i] = j;
		error = "live nodes require unique, matching :id keys";
		if (map[i] < 0 || used[map[i]])
			goto fail;
		used[map[i]] = 1;
	}
	error = "live update changes the audio output";
	if (map[next->output] != active->output)
		goto fail;
	for (int i = 0; i < next->nnodes; ++i) {
		const Node *a = next->nodes + i, *b = active->nodes + map[i];
		error = "live update changes a node; stop before changing the graph";
		if (!same(a->path, b->path) || !same(a->name, b->name) || a->channels != b->channels || a->track != b->track ||
		    a->ninputs != b->ninputs || !same_config(&a->config, &b->config))
			goto fail;
		for (int j = 0; j < a->ninputs; ++j) {
			error = "live update changes routing; stop before changing the graph";
			if (map[a->inputs[j]] != b->inputs[j])
				goto fail;
		}
	}
	uint64_t at = sequence_boundary(&next->sequence, s->sample_rate, earliest);
	error = "no update boundary before the end of the score";
	if (at == UINT64_MAX || at >= s->frames)
		goto fail;
	Revision *r = calloc(1, sizeof(*r));
	error = "out of memory";
	if (!r)
		goto fail;
	r->score = *next;
	if (sequencer_init(&r->cursor, &r->score.sequence, s->sample_rate, at)) {
		free(r);
		goto fail;
	}
	score_remap(&r->score, map);
	r->at = at;
	r->revision = revision;
	if (mapping)
		memcpy(mapping, map, s->nnodes * sizeof(int));
	*next = (Score){0};
	atomic_store(&s->pending, r);
	return 0;
fail:
	next->error = error;
	return -1;
}

int session_render(Session *s, float *out, size_t frames) {
	if (!s->audio || frames > s->frames - s->time)
		return fail(s, "render outside session");
	Revision *r = s->active;
	Sequencer *cursor = &r->cursor;
	bw_slew_lim_coeffs slew;
	bw_slew_lim_init(&slew);
	bw_slew_lim_set_sample_rate(&slew, s->sample_rate);
	bw_slew_lim_set_max_rate(&slew, 1.f / .005f);
	bw_slew_lim_reset_coeffs(&slew);
	while (frames) {
		session_sync(s);
		Revision *pending = atomic_load(&s->pending);
		if (pending && pending->at <= s->time) {
			// If publication missed its boundary, keep the old revision until the next grid.
			// The editor schedules with a safety margin; this also covers a stalled control thread.
			if (pending->at < s->time) {
				pending->at = sequence_boundary(&pending->score.sequence, s->sample_rate, s->time);
				sequencer_seek(&pending->cursor, pending->at);
			} else {
				for (int i = 0; i < s->nnodes; ++i) {
					const Node *n = r->score.nodes + i;
					int count = n->config.nparams;
					for (int j = 0; j < count; ++j) {
						if ((!n->path || !(n->config.outputs & (UINT64_C(1) << j))) &&
						    pending->score.nodes[i].config.defaults[j] != n->config.defaults[j])
							set_control(s, i, j, pending->score.nodes[i].config.defaults[j]);
					}
				}
				Revision *old = r;
				s->active = r = pending;
				cursor = &r->cursor;
				atomic_store(&s->retired, old);
				atomic_store(&s->revision, r->revision);
				atomic_store(&s->pending, NULL);
				pending = NULL;
			}
		}
		// Parameters precede note-offs and note-ons at a shared sample.
		while (sequencer_next(cursor) <= s->time && sequencer_peek(cursor, NULL)->parameter >= 0) {
			const Cue *cue = sequencer_peek(cursor, NULL);
			set_control(s, cue->node, cue->parameter, cue->value);
			sequencer_pop(cursor);
		}
		if (s->next_off <= s->time) {
			s->next_off = UINT64_MAX;
			for (int i = 0; i < s->nnodes; ++i)
				for (int pitch = 0; pitch < 128; ++pitch) {
					uint64_t *end = &s->held[i][pitch];
					if (*end && *end <= s->time) {
						note_message(s->nodes + i, pitch, 0);
						*end = 0;
					}
					if (*end && *end < s->next_off)
						s->next_off = *end;
				}
		}
		while (sequencer_next(cursor) <= s->time) {
			uint64_t end;
			const Cue *cue = sequencer_peek(cursor, &end);
			uint64_t *held = &s->held[cue->node][cue->pitch];
			if (*held)
				note_message(s->nodes + cue->node, cue->pitch, 0);
			note_message(s->nodes + cue->node, cue->pitch, cue->velocity);
			*held = end;
			if (end < s->next_off)
				s->next_off = end;
			sequencer_pop(cursor);
		}
		uint64_t next = sequencer_next(cursor);
		if (s->next_off < next)
			next = s->next_off;
		if (pending && pending->at < next)
			next = pending->at;
		size_t n = frames < BLOCK ? frames : BLOCK;
		if (next - s->time < n)
			n = next - s->time;
		if (render_audio(s, &r->score, &slew, out, n))
			return -1;
		out += 2 * n;
		frames -= n;
	}
	return 0;
}
