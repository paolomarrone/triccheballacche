#include "session.h"
#include "util.h"
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
	if (!s->sample_rate)
		s->sample_rate = DEFAULT_SAMPLE_RATE;
	return s->sample_rate <= 384000 ? s->sample_rate : 0;
}

static int valid(Session *s, int id, int param, float value) {
	if (s->sealed || id < 0 || id >= s->nnodes)
		return fail(s, "sealed session or invalid handle");
	const Node *n = s->nodes + id;
	const PluginConfig *config = n->path ? &n->dsp[0].config : NULL;
	if (param < 0 || param >= (config ? config->nparams : 2) || (config && (config->outputs & (UINT64_C(1) << param))))
		return fail(s, "unknown or output parameter");
	if (!isfinite(value) || (!config && (value < (param ? -1 : 0) || value > (param ? 1 : 4))))
		return fail(s, "parameter outside range");
	return 0;
}

int session_plugin(Session *s, const char *path, const PluginConfig *config) {
	if (s->sealed || s->nnodes == MAX_NODES)
		return fail(s, "sealed session or node limit reached");
	if (!engine_config_valid(config))
		return fail(s, "invalid plugin configuration");
	Node *n = s->nodes + s->nnodes;
	*n = (Node){0};
	n->dsp[0].config = *config;
	n->path = copy_string(path);
	if (!n->path)
		return fail(s, "out of memory");
	return s->nnodes++;
}

int session_set(Session *s, int id, int param, float value) {
	if (valid(s, id, param, value))
		return -1;
	Node *n = s->nodes + id;
	if (n->path)
		n->dsp[0].config.defaults[param] = value;
	else
		n->defaults[param] = value;
	return 0;
}

static int handle(Session *s, int id) {
	return !s->sealed && id >= 0 && id < s->nnodes;
}

int session_through(Session *s, int source, int effect) {
	if (!handle(s, source) || !handle(s, effect))
		return fail(s, "sealed session or invalid handle");
	Node *n = s->nodes + effect;
	if (!n->path || !n->dsp[0].config.input || n->ninputs)
		return fail(s, "expected an effect without an input");
	n->inputs = malloc(sizeof(Input));
	if (!n->inputs)
		return fail(s, "out of memory");
	n->inputs[0] = (Input){.node = source};
	n->ninputs = 1;
	return effect;
}

int session_mix(Session *s, const int *inputs, int count) {
	if (s->sealed || s->nnodes == MAX_NODES || count < 1 || count > MAX_NODES)
		return fail(s, "sealed session or invalid mix size");
	for (int i = 0; i < count; ++i)
		if (!handle(s, inputs[i]))
			return fail(s, "invalid mix input");
	Node *n = s->nodes + s->nnodes;
	*n = (Node){.ninputs = count, .defaults = {1, 0}};
	n->inputs = calloc(count, sizeof(Input));
	if (!n->inputs)
		return fail(s, "out of memory");
	for (int i = 0; i < count; ++i)
		n->inputs[i].node = inputs[i];
	return s->nnodes++;
}

int session_output(Session *s, int source) {
	if (!handle(s, source) || s->has_output)
		return fail(s, "expected one output in an unsealed session");
	s->output = source;
	s->has_output = 1;
	return source;
}

// A track is a visible mixer point. Its source may itself be a mix or another track.
int session_track(Session *s, int source, const int *effects, int count, int master) {
	if (!handle(s, source) || count < 0 || count > MAX_FX || (master ? s->has_master : s->ntracks == MAX_TRACKS))
		return fail(s, "sealed session or track/effect limit reached");
	int signal = source, connected = 0;
	for (; connected < count; ++connected) {
		signal = session_through(s, signal, effects[connected]);
		if (signal < 0)
			goto rollback;
	}
	int mixer = session_mix(s, &signal, 1);
	if (mixer < 0)
		goto rollback;
	Track *t = master ? &s->master : &s->tracks[s->ntracks++];
	*t = (Track){.source = source, .mixer = mixer, .count = count};
	atomic_init(&t->listen, 0);
	for (int i = 0; i < count; ++i)
		t->effects[i] = effects[i];
	if (master)
		s->has_master = 1;
	else
		s->nodes[mixer].track = s->ntracks;
	return mixer;
rollback:
	while (connected) {
		Node *n = s->nodes + effects[--connected];
		free(n->inputs);
		n->inputs = NULL;
		n->ninputs = 0;
	}
	return -1;
}

// Validate and order only at sealing: plugins may be declared before their inputs.
static int visit(Session *s, int id, unsigned char *state) {
	if (state[id] == 1)
		return fail(s, "audio graph contains a cycle");
	if (state[id] == 2)
		return 0;
	state[id] = 1;
	Node *n = s->nodes + id;
	if (n->path && n->ninputs != !!n->dsp[0].config.input)
		return fail(s, "effect has no input");
	n->upstream = n->track ? 1u << (n->track - 1) : 0;
	for (int i = 0; i < n->ninputs; ++i) {
		int input = n->inputs[i].node;
		if (visit(s, input, state))
			return -1;
		n->upstream |= s->nodes[input].upstream;
	}
	n->channels = n->path ? n->dsp[0].config.output : 2;
	if (n->path && n->ninputs && s->nodes[n->inputs[0].node].channels == 2 && n->dsp[0].config.input == 1) {
		if (n->channels == 2)
			return fail(s, "a mono-to-stereo effect requires a mono signal");
		n->channels = 2;
	}
	state[id] = 2;
	s->order[s->norder++] = id;
	return 0;
}

static int compile(Session *s) {
	if (!s->has_output)
		return fail(s, "missing audio output");
	unsigned char state[MAX_NODES] = {0};
	s->norder = 0;
	if (visit(s, s->output, state))
		return -1;
	if (s->norder != s->nnodes)
		return fail(s, "node does not reach the output");
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		n->downstream = n->track ? 1u << (n->track - 1) : 0;
	}
	for (int i = s->norder; i-- > 0;) {
		Node *n = s->nodes + s->order[i];
		for (int j = 0; j < n->ninputs; ++j)
			s->nodes[n->inputs[j].node].downstream |= n->downstream;
	}
	return 0;
}

static int reserve(Session *s, Node *n, size_t extra) {
	if (extra > SIZE_MAX - n->count)
		return fail(s, "too many events");
	size_t need = n->count + extra;
	if (need <= n->capacity)
		return 0;
	size_t cap = n->capacity ? n->capacity * 2 : 128;
	if (cap < need)
		cap = need;
	if (cap > SIZE_MAX / sizeof(Event))
		return fail(s, "too many events");
	Event *events = realloc(n->events, cap * sizeof(*events));
	if (!events)
		return fail(s, "out of memory");
	n->events = events;
	n->capacity = cap;
	return 0;
}

int session_param(Session *s, int id, size_t time, int param, float value) {
	if (valid(s, id, param, value))
		return -1;
	Node *n = s->nodes + id;
	if (reserve(s, n, 1))
		return -1;
	n->events[n->count] = (Event){time, param, value, {0}, n->count};
	++n->count;
	return 0;
}

int session_note(Session *s, int id, size_t time, size_t end, int pitch, int velocity) {
	if (s->sealed || id < 0 || id >= s->nnodes || end <= time || pitch < 0 || pitch > 127 || velocity < 1 ||
	    velocity > 127)
		return fail(s, "invalid note or sealed session");
	Node *n = s->nodes + id;
	if (!n->path || n->dsp[0].config.midi < 0)
		return fail(s, "node has no MIDI input");
	if (reserve(s, n, 2))
		return -1;
	n->events[n->count] = (Event){time, -1, 0, {0x90, pitch, velocity}, n->count};
	++n->count;
	n->events[n->count] = (Event){end, -1, 0, {0x80, pitch, 0}, n->count};
	++n->count;
	return 0;
}

static int compare(const void *aa, const void *bb) {
	const Event *a = aa, *b = bb;
	if (a->time != b->time)
		return a->time < b->time ? -1 : 1;
	int pa = a->parameter >= 0 ? 0 : a->midi[0], pb = b->parameter >= 0 ? 0 : b->midi[0];
	if (pa != pb)
		return pa < pb ? -1 : 1;
	return (a->order > b->order) - (a->order < b->order);
}

int session_end(Session *s, uint64_t frames) {
	if (s->sealed || !frames || !session_rate(s))
		return fail(s, "empty duration or already sealed");
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		for (size_t j = 0; j < n->count; ++j) {
			Event *e = n->events + j;
			if (e->time > frames || (e->time == frames && (e->parameter >= 0 || e->midi[0] != 0x80)))
				return fail(s, "event outside duration");
		}
	}
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		if (n->count)
			qsort(n->events, n->count, sizeof(Event), compare);
	}
	if (compile(s))
		return -1;
	s->frames = frames;
	s->sealed = 1;
	Sequence *sequence = s->sequence;
	if (sequence) {
		if (sequence_prepare(sequence, session_rate(s), 0))
			return fail(s, "invalid sequence timing or out of memory");
		sequence->defaults = calloc(s->nnodes * MAX_PARAMS, sizeof(float));
		if (!sequence->defaults)
			return fail(s, "out of memory");
		for (int i = 0; i < s->nnodes; ++i) {
			Node *node = s->nodes + i;
			memcpy(sequence->defaults + i * MAX_PARAMS, node->path ? node->dsp[0].config.defaults : node->defaults,
			    (node->path ? node->dsp[0].config.nparams : 2) * sizeof(float));
		}
	}
	return s->describe ? 0 : session_activate(s);
}

int session_listen(Session *s, int track, int flags) {
	if (!s->sealed || track < 0 || track >= s->ntracks || flags < 0 || flags > (TRACK_MUTE | TRACK_SOLO))
		return -1;
	atomic_store_explicit(&s->tracks[track].listen, flags, memory_order_relaxed);
	return 0;
}

static void set_control(Session *s, int id, int parameter, float value) {
	Node *n = s->nodes + id;
	if (!n->path)
		n->values[parameter] = value;
	else
		for (int c = 0; c < 2 && n->dsp[c].dsp; ++c)
			set_dsp(n->dsp[c].dsp, parameter, value);
}

uint64_t session_frame(const Session *s, double seconds) {
	double value = seconds * s->sample_rate;
	if (!s->sealed || !isfinite(value) || value < 0 || value >= 0x1p53)
		return UINT64_MAX;
	uint64_t frame = llround(value);
	return frame <= s->frames ? frame : UINT64_MAX;
}

int session_seek(Session *s, uint64_t from) {
	if (!s->sealed || !s->audio)
		return fail(s, "seek requires a prepared session");
	if (from > s->frames || from >= (UINT64_C(1) << 53))
		return fail(s, "position outside score");
	// At most one final MIDI state per pitch. Keep onset order for monophonic note priority.
	Event(*notes)[128] = from ? calloc(s->nnodes, sizeof(*notes)) : NULL;
	if (from && !notes)
		return fail(s, "out of memory");
	float values[MAX_NODES][MAX_PARAMS];
	Sequence *sequence = s->sequence;
	session_cancel(s);
	if (sequence) {
		sequence->at = 0;
		memset(s->held, 0, s->nnodes * sizeof(*s->held));
		s->next_off = UINT64_MAX;
	}
	unsigned solo = 0;
	for (int i = 0; i < s->ntracks; ++i) {
		int flags = atomic_load(&s->tracks[i].listen);
		s->tracks[i].level = !(flags & TRACK_MUTE);
		if (flags & TRACK_SOLO)
			solo |= 1u << i;
	}
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		const PluginConfig *config = &n->dsp[0].config;
		const float *defaults = sequence ? sequence->defaults + i * MAX_PARAMS
		    : n->path                    ? config->defaults
		                                 : n->defaults;
		memcpy(values[i], defaults, (n->path ? config->nparams : 2) * sizeof(float));
		for (int j = 0; j < n->ninputs; ++j) {
			Input *edge = n->inputs + j;
			edge->level = !solo || ((s->nodes[edge->node].upstream | n->downstream) & solo);
		}
		n->next = 0;
		while (n->next < n->count && n->events[n->next].time < from) {
			const Event *e = n->events + n->next++;
			if (e->parameter >= 0)
				values[i][e->parameter] = e->value;
			else {
				notes[i][e->midi[1]] = *e;
				notes[i][e->midi[1]].time = 0;
				notes[i][e->midi[1]].order = n->next;
			}
		}
	}
	if (sequence) {
		sequence_history(sequence, from);
		for (size_t order = 0; sequence->used; ++order) {
			uint64_t end;
			const Cue *cue = sequence_peek(sequence, &end);
			if (cue->parameter >= 0)
				values[cue->node][cue->parameter] = cue->value;
			else {
				int active = end > from;
				notes[cue->node][cue->pitch] = (Event){.parameter = -1,
				    .order = order,
				    .midi = {active ? 0x90 : 0x80, cue->pitch, active ? cue->velocity : 0}};
				s->held[cue->node][cue->pitch] = active ? end : 0;
			}
			sequence_shift(sequence);
		}
		sequence_seek(sequence, from);
		for (int i = 0; i < s->nnodes; ++i)
			for (int pitch = 0; pitch < 128; ++pitch)
				if (s->held[i][pitch] && s->held[i][pitch] < s->next_off)
					s->next_off = s->held[i][pitch];
	}
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		const PluginConfig *config = &n->dsp[0].config;
		for (int j = 0; j < (n->path ? config->nparams : 2); ++j)
			if (!n->path || !(config->outputs & (UINT64_C(1) << j)))
				set_control(s, i, j, values[i][j]);
		for (int c = 0; c < 2 && n->dsp[c].dsp; ++c) {
			Engine *e = n->dsp + c;
			e->next = n->next;
			e->time = from;
			// Reset after restoring parameters; history and queued UI gestures belong to the old position.
			reset_dsp(e->dsp);
			e->events = n->events;
			e->count = n->count;
		}
		if (notes && n->path && n->dsp[0].config.midi >= 0) {
			qsort(notes[i], 128, sizeof(Event), compare);
			for (int j = 0; j < 128; ++j)
				if (notes[i][j].parameter < 0)
					midi_dsp(n->dsp[0].dsp, n->dsp[0].config.midi, notes[i][j].midi);
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

static void controls(Node *n, uint64_t time) {
	while (n->next < n->count && n->events[n->next].time <= time) {
		Event *e = n->events + n->next++;
		n->values[e->parameter] = e->value;
	}
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

static void input_audio(Session *s, Node *node, int index, unsigned solo, const bw_slew_lim_coeffs *slew, float *audio,
    size_t offset, size_t frames) {
	Input *edge = node->inputs + index;
	Node *source = s->nodes + edge->node;
	const float *input = s->audio + edge->node * BLOCK * 2 + offset * source->channels;
	float target = !solo || ((source->upstream | node->downstream) & solo);
	if (!s->time)
		edge->level = target;
	bw_buf_copy(input, audio, frames * source->channels);
	fade(slew, &edge->level, target, audio, source->channels, frames);
}

static int process(Session *s, Node *node, unsigned solo, const bw_slew_lim_coeffs *slew, float *out, size_t frames) {
	float input[BLOCK * 2];
	if (!node->ninputs) {
		render(node->dsp, out, NULL, frames);
		return 0;
	}
	if (!node->path) {
		bw_pan_coeffs panner;
		bw_pan_init(&panner);
		bw_pan_set_sample_rate(&panner, s->sample_rate);
		bw_balance_coeffs balance;
		bw_balance_init(&balance);
		bw_balance_set_sample_rate(&balance, s->sample_rate);
		bw_buf_fill(0, out, 2 * frames);
		// Process constant-control spans, sharing the same controls across every input.
		for (size_t i = 0; i < frames;) {
			controls(node, s->time + i);
			size_t n = frames - i;
			if (node->next < node->count && node->events[node->next].time - s->time - i < n)
				n = node->events[node->next].time - s->time - i;
			float gain = node->values[0], pan = node->values[1], left, right;
			// Reset at each event: score automation is immediate, without BW's default smoothing.
			bw_pan_set_pan(&panner, pan);
			bw_pan_reset_coeffs(&panner);
			bw_balance_set_balance(&balance, pan);
			bw_balance_reset_coeffs(&balance);
			bw_balance_process1(&balance, 1, 1, &left, &right);
			for (int j = 0; j < node->ninputs; ++j) {
				input_audio(s, node, j, solo, slew, input, i, n);
				int channels = s->nodes[node->inputs[j].node].channels;
				// Stateless buffer gain preserves exact steps and seek behavior.
				bw_buf_scale(input, gain, input, channels * n);
				if (!bw_has_only_finite(input, channels * n))
					return fail(s, "non-finite audio");
				if (channels == 1) {
					for (size_t k = n; k-- > 0;) {
						float x = input[k];
						bw_pan_process1(&panner, x, input + 2 * k, input + 2 * k + 1);
					}
				} else {
					for (size_t k = 0; k < n; ++k) {
						input[2 * k] *= left;
						input[2 * k + 1] *= right;
					}
				}
				bw_buf_mix(out + 2 * i, input, out + 2 * i, 2 * n);
			}
			i += n;
		}
		return 0;
	}
	input_audio(s, node, 0, solo, slew, input, 0, frames);
	if (node->dsp[1].dsp) {
		float mono[BLOCK], tmp[BLOCK];
		for (int c = 0; c < 2; ++c) {
			for (size_t i = 0; i < frames; ++i)
				mono[i] = input[2 * i + c];
			render(node->dsp + c, tmp, mono, frames);
			for (size_t i = 0; i < frames; ++i)
				out[2 * i + c] = tmp[i];
		}
	} else {
		if (s->nodes[node->inputs[0].node].channels == 1 && node->dsp[0].config.input == 2)
			for (size_t i = frames; i-- > 0;)
				input[2 * i] = input[2 * i + 1] = input[i];
		render(node->dsp, out, input, frames);
	}
	return 0;
}

static int render_audio(Session *s, float *out, size_t frames) {
	if (!s->sealed || s->describe || frames > s->frames - s->time)
		return fail(s, "render outside session");
	bw_slew_lim_coeffs slew;
	bw_slew_lim_init(&slew);
	bw_slew_lim_set_sample_rate(&slew, s->sample_rate);
	bw_slew_lim_set_max_rate(&slew, 1.f / .005f);
	bw_slew_lim_reset_coeffs(&slew);
	while (frames) {
		size_t n = frames < BLOCK ? frames : BLOCK;
		unsigned solo = 0, mute = 0;
		for (int i = 0; i < s->ntracks; ++i) {
			int flags = atomic_load_explicit(&s->tracks[i].listen, memory_order_relaxed);
			if (flags & TRACK_SOLO)
				solo |= 1u << i;
			if (flags & TRACK_MUTE)
				mute |= 1u << i;
		}
		if (!s->sequence)
			session_sync(s);
		for (int i = 0; i < s->norder; ++i) {
			int id = s->order[i];
			Node *node = s->nodes + id;
			float *audio = s->audio + id * BLOCK * 2;
			if (process(s, node, solo, &slew, audio, n))
				return -1;
			if (node->track) {
				int j = node->track - 1;
				Track *track = s->tracks + j;
				float target = !(mute & (1u << j));
				if (!s->time)
					track->level = target;
				fade(&slew, &track->level, target, audio, 2, n);
			}
			for (size_t j = 0; j < n * node->channels; ++j)
				if (!isfinite(audio[j]))
					return fail(s, "non-finite audio");
		}
		const float *audio = s->audio + s->output * BLOCK * 2;
		if (s->nodes[s->output].channels == 1)
			for (size_t i = 0; i < n; ++i)
				out[2 * i] = out[2 * i + 1] = audio[i];
		else
			memcpy(out, audio, 2 * n * sizeof(float));
		s->time += n;
		frames -= n;
		out += 2 * n;
	}
	return 0;
}

void session_free(Session *s) {
	sequence_free(s->sequence);
	sequence_free(s->pending);
	sequence_free(s->retired);
	free(s->held);
	while (s->nnodes) {
		Node *n = s->nodes + --s->nnodes;
		close_engine(n->dsp);
		close_engine(n->dsp + 1);
		free(n->events);
		free(n->path);
		free(n->name);
		free(n->key);
		free(n->inputs);
	}
	free(s->audio);
	*s = (Session){0};
}

int session_activate(Session *s) {
	if (!s->sealed || s->audio)
		return fail(s, "activation requires a prepared description");
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		if (!n->path)
			continue;
		PluginConfig config = n->dsp[0].config;
		int count = n->channels == 2 && config.output == 1 ? 2 : 1;
		for (int c = 0; c < count; ++c) {
			n->dsp[c].modules = s->modules;
			if (!n->dsp[c].dsp && open_engine(n->dsp + c, n->path, &config, session_rate(s)))
				return fail(s, "cannot open plugin");
		}
	}
	s->audio = calloc(s->nnodes, BLOCK * 2 * sizeof(float));
	if (s->sequence)
		s->held = calloc(s->nnodes, sizeof(*s->held));
	if (!s->audio || (s->sequence && !s->held))
		return fail(s, "out of memory");
	s->describe = 0;
	return session_seek(s, 0);
}

void session_cancel(Session *s) {
	sequence_free(atomic_exchange(&s->pending, NULL));
	session_collect(s);
}

void session_collect(Session *s) {
	sequence_free(atomic_exchange(&s->retired, NULL));
}

static int same(const char *a, const char *b) {
	return (!a && !b) || (a && b && !strcmp(a, b));
}

static int same_config(const PluginConfig *a, const PluginConfig *b) {
	return a->input == b->input && a->output == b->output && a->midi == b->midi && a->input_offset == b->input_offset &&
	    a->inputs == b->inputs && a->nparams == b->nparams && a->outputs == b->outputs && a->to_ui == b->to_ui &&
	    a->to_dsp == b->to_dsp;
}

int session_update(Session *s, Session *description, uint64_t earliest, unsigned revision, int *mapping) {
	Sequence *active = atomic_load(&s->sequence), *next = description->sequence;
	if (!s->audio || !active || !next || !description->sealed || !description->describe ||
	    s->sample_rate != description->sample_rate || s->frames != description->frames ||
	    s->has_master != description->has_master || s->nnodes != description->nnodes ||
	    s->ntracks != description->ntracks || active->bpm != next->bpm || atomic_load(&s->pending))
		return fail(description, "live update requires the same graph, duration and tempo, with no pending revision");
	session_collect(s);
	int map[MAX_NODES];
	unsigned char used[MAX_NODES] = {0};
	for (int i = 0; i < description->nnodes; ++i) {
		Node *a = description->nodes + i;
		map[i] = -1;
		if (a->key)
			for (int j = 0; j < s->nnodes; ++j)
				if (same(a->key, s->nodes[j].key))
					map[i] = j;
		if (map[i] < 0 || used[map[i]])
			return fail(description, "live nodes require unique, matching :id keys");
		used[map[i]] = 1;
	}
	if (map[description->output] != s->output)
		return fail(description, "live update changes the audio output");
	for (int i = 0; i < description->nnodes; ++i) {
		Node *a = description->nodes + i, *b = s->nodes + map[i];
		if (!same(a->path, b->path) || !same(a->name, b->name) || a->channels != b->channels || a->track != b->track ||
		    a->ninputs != b->ninputs || !same_config(&a->dsp[0].config, &b->dsp[0].config))
			return fail(description, "live update changes a node; stop before changing the graph");
		for (int j = 0; j < a->ninputs; ++j)
			if (map[a->inputs[j].node] != b->inputs[j].node)
				return fail(description, "live update changes routing; stop before changing the graph");
	}
	uint64_t at = sequence_boundary(next, earliest);
	if (at == UINT64_MAX || at >= s->frames)
		return fail(description, "no update boundary before the end of the score");
	float *defaults = calloc(s->nnodes * MAX_PARAMS, sizeof(float));
	if (!defaults)
		return fail(description, "out of memory");
	for (int i = 0; i < s->nnodes; ++i)
		memcpy(defaults + map[i] * MAX_PARAMS, next->defaults + i * MAX_PARAMS, MAX_PARAMS * sizeof(float));
	free(next->defaults);
	next->defaults = defaults;
	for (size_t i = 0; i < next->count; ++i)
		next->cues[i].node = map[next->cues[i].node];
	if (mapping)
		memcpy(mapping, map, s->nnodes * sizeof(int));
	next->at = at;
	next->revision = revision;
	sequence_seek(next, next->at);
	description->sequence = NULL;
	atomic_store(&s->pending, next);
	return 0;
}

static void note_message(Node *n, int pitch, int velocity) {
	uint8_t message[] = {velocity ? 0x90 : 0x80, pitch, velocity};
	midi_dsp(n->dsp[0].dsp, n->dsp[0].config.midi, message);
}

int session_render(Session *s, float *out, size_t frames) {
	Sequence *sequence = s->sequence;
	if (!sequence)
		return render_audio(s, out, frames);
	if (!s->sealed || s->describe || frames > s->frames - s->time)
		return fail(s, "render outside session");
	while (frames) {
		session_sync(s);
		Sequence *pending = atomic_load(&s->pending);
		if (pending && pending->at <= s->time) {
			// If publication missed its boundary, keep the old revision until the next grid.
			// The editor schedules with a safety margin; this also covers a stalled control thread.
			if (pending->at < s->time) {
				pending->at = sequence_boundary(pending, s->time);
				sequence_seek(pending, pending->at);
			} else {
				for (int i = 0; i < s->nnodes; ++i) {
					Node *n = s->nodes + i;
					int count = n->path ? n->dsp[0].config.nparams : 2;
					for (int j = 0; j < count; ++j) {
						size_t k = i * MAX_PARAMS + j;
						if ((!n->path || !(n->dsp[0].config.outputs & (UINT64_C(1) << j))) &&
						    pending->defaults[k] != sequence->defaults[k])
							set_control(s, i, j, pending->defaults[k]);
					}
				}
				atomic_store(&s->retired, sequence);
				s->sequence = sequence = pending;
				atomic_store(&s->revision, sequence->revision);
				atomic_store(&s->pending, NULL);
				pending = NULL;
			}
		}
		// Parameters precede note-offs and note-ons at a shared sample.
		while (sequence_next(sequence) <= s->time && sequence_peek(sequence, NULL)->parameter >= 0) {
			const Cue *cue = sequence_peek(sequence, NULL);
			set_control(s, cue->node, cue->parameter, cue->value);
			sequence_pop(sequence);
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
		while (sequence_next(sequence) <= s->time) {
			uint64_t end;
			const Cue *cue = sequence_peek(sequence, &end);
			uint64_t *held = &s->held[cue->node][cue->pitch];
			if (*held)
				note_message(s->nodes + cue->node, cue->pitch, 0);
			note_message(s->nodes + cue->node, cue->pitch, cue->velocity);
			*held = end;
			if (end < s->next_off)
				s->next_off = end;
			sequence_pop(sequence);
		}
		uint64_t next = sequence_next(sequence);
		if (s->next_off < next)
			next = s->next_off;
		if (pending && pending->at < next)
			next = pending->at;
		size_t n = frames < BLOCK ? frames : BLOCK;
		if (next - s->time < n)
			n = next - s->time;
		if (render_audio(s, out, n))
			return -1;
		out += 2 * n;
		frames -= n;
	}
	return 0;
}
