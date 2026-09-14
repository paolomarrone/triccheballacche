#include "session.h"
#include "util.h"
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
	if (param < 0 || param >= (config ? config->nparams : n->ncontrols) ||
	    (config && (config->outputs & (UINT64_C(1) << param))))
		return fail(s, "unknown or output parameter");
	if (!isfinite(value) || (!config && (value < (param ? -1 : 0) || value > (param ? 1 : 4))))
		return fail(s, "parameter outside range");
	return 0;
}

int session_plugin(Session *s, const char *path, const PluginConfig *config) {
	if (s->sealed || s->nnodes == MAX_NODES)
		return fail(s, "sealed session or node limit reached");
	Node *n = s->nodes + s->nnodes;
	*n = (Node){0};
	n->dsp[0].modules = s->modules;
	if (open_engine(n->dsp, path, config, session_rate(s))) {
		close_engine(n->dsp);
		return fail(s, "cannot open plugin");
	}
	n->path = copy_string(path);
	if (!n->path) {
		close_engine(n->dsp);
		return fail(s, "out of memory");
	}
	return s->nnodes++;
}

int session_set(Session *s, int id, int param, float value) {
	if (valid(s, id, param, value))
		return -1;
	Node *n = s->nodes + id;
	if (n->path)
		n->dsp[0].config.defaults[param] = value;
	else
		n->defaults[param] = n->values[param] = value;
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
	*n = (Node){.ncontrols = 2, .ninputs = count, .defaults = {1, 0}, .values = {1, 0}};
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
		if (n->path && n->channels == 2 && n->dsp[0].config.output == 1 && !n->dsp[1].dsp) {
			n->dsp[1].modules = s->modules;
			if (open_engine(n->dsp + 1, n->path, &n->dsp[0].config, session_rate(s)))
				return fail(s, "cannot create second mono effect instance");
		}
	}
	if (!s->audio)
		s->audio = calloc(s->nnodes, BLOCK * 2 * sizeof(float));
	return s->audio ? 0 : fail(s, "out of memory");
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

int session_end(Session *s, size_t frames) {
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
	return session_rewind(s);
}

int session_listen(Session *s, int track, int flags) {
	if (!s->sealed || track < 0 || track >= s->ntracks || flags < 0 || flags > (TRACK_MUTE | TRACK_SOLO))
		return -1;
	atomic_store_explicit(&s->tracks[track].listen, flags, memory_order_relaxed);
	return 0;
}

int session_rewind(Session *s) {
	if (!s->sealed)
		return fail(s, "rewind requires a prepared session");
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		n->next = 0;
		memcpy(n->values, n->defaults, sizeof(n->values));
		if (!n->path)
			continue;
		const PluginConfig *config = &n->dsp[0].config;
		for (int c = 0; c < 2 && n->dsp[c].dsp; ++c) {
			Engine *e = n->dsp + c;
			e->next = e->time = 0;
			for (int j = 0; j < config->nparams; ++j)
				if (!(config->outputs & (UINT64_C(1) << j)))
					set_dsp(e->dsp, j, config->defaults[j]);
			reset_dsp(e->dsp);
			e->events = n->events;
			e->count = n->count;
		}
	}
	s->time = 0;
	s->error = NULL;
	return 0;
}

void session_sync(Session *s) {
	for (int i = 0; i < s->nnodes; ++i)
		sync_dsp(s->nodes[i].dsp[0].dsp, s->nodes[i].dsp[1].dsp);
}

static void controls(Node *n, size_t time) {
	while (n->next < n->count && n->events[n->next].time <= time) {
		Event *e = n->events + n->next++;
		n->values[e->parameter] = e->value;
	}
}

static float fade(float level, float target, float step) {
	return level < target ? fminf(target, level + step) : fmaxf(target, level - step);
}

static void input_audio(Session *s, Node *node, int index, unsigned solo, float *audio, size_t frames) {
	Input *edge = node->inputs + index;
	Node *source = s->nodes + edge->node;
	const float *input = s->audio + edge->node * BLOCK * 2;
	float target = !solo || ((source->upstream | node->downstream) & solo);
	float step = 1.f / (.005f * s->sample_rate);
	if (!s->time)
		edge->level = target;
	for (size_t i = 0; i < frames; ++i) {
		edge->level = fade(edge->level, target, step);
		for (int c = 0; c < source->channels; ++c)
			audio[i * source->channels + c] = input[i * source->channels + c] * edge->level;
	}
}

static void process(Session *s, Node *node, unsigned solo, float *out, size_t frames) {
	float input[BLOCK * 2];
	if (!node->ninputs) {
		render(node->dsp, out, NULL, frames);
		return;
	}
	if (!node->path) {
		// Cache sample-accurate controls once, then sum each input with its channel layout.
		float gain[BLOCK], pan[BLOCK];
		for (size_t i = 0; i < frames; ++i) {
			controls(node, s->time + i);
			gain[i] = node->values[0];
			pan[i] = node->values[1];
		}
		memset(out, 0, 2 * frames * sizeof(float));
		for (int j = 0; j < node->ninputs; ++j) {
			input_audio(s, node, j, solo, input, frames);
			int channels = s->nodes[node->inputs[j].node].channels;
			for (size_t i = 0; i < frames; ++i) {
				if (channels == 1) {
					float angle = (pan[i] + 1) * .7853981633974483f, x = input[i] * gain[i];
					out[2 * i] += x * cosf(angle);
					out[2 * i + 1] += x * sinf(angle);
				} else {
					out[2 * i] += input[2 * i] * gain[i] * (pan[i] > 0 ? 1 - pan[i] : 1);
					out[2 * i + 1] += input[2 * i + 1] * gain[i] * (pan[i] < 0 ? 1 + pan[i] : 1);
				}
			}
		}
		return;
	}
	input_audio(s, node, 0, solo, input, frames);
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
}

int session_render(Session *s, float *out, size_t frames) {
	if (!s->sealed || frames > s->frames - s->time)
		return fail(s, "render outside session");
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
		session_sync(s);
		for (int i = 0; i < s->norder; ++i) {
			int id = s->order[i];
			Node *node = s->nodes + id;
			float *audio = s->audio + id * BLOCK * 2;
			process(s, node, solo, audio, n);
			if (node->track) {
				int j = node->track - 1;
				Track *track = s->tracks + j;
				float target = !(mute & (1u << j)), step = 1.f / (.005f * s->sample_rate);
				if (!s->time)
					track->level = target;
				for (size_t k = 0; k < n; ++k) {
					track->level = fade(track->level, target, step);
					audio[2 * k] *= track->level;
					audio[2 * k + 1] *= track->level;
				}
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
	while (s->nnodes) {
		Node *n = s->nodes + --s->nnodes;
		close_engine(n->dsp);
		close_engine(n->dsp + 1);
		free(n->events);
		free(n->path);
		free(n->name);
		free(n->inputs);
	}
	free(s->audio);
	*s = (Session){0};
}
