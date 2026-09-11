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
		n->values[param] = value;
	return 0;
}

int session_track(Session *s, int source, const int *effects, int count, int master) {
	if (s->sealed || s->nnodes == MAX_NODES || count < 0 || count > MAX_FX ||
	    (master ? s->has_master : s->ntracks == MAX_TRACKS))
		return fail(s, "sealed session or track/effect limit reached");
	int ids[MAX_FX + 1], total = 0;
	if (!master)
		ids[total++] = source;
	for (int i = 0; i < count; ++i)
		ids[total++] = effects[i];
	for (int i = 0; i < total; ++i) {
		int id = ids[i];
		if (id < 0 || id >= s->nnodes || !s->nodes[id].path || s->nodes[id].attached ||
		    !!s->nodes[id].dsp[0].config.input != (master || i > 0))
			return fail(s, "expected unused source/effect plugin");
		for (int j = 0; j < i; ++j)
			if (ids[j] == id)
				return fail(s, "plugin already in this chain");
	}
	int channels = master ? 2 : s->nodes[source].dsp[0].config.output, duplicate[MAX_FX];
	for (int i = 0; i < count; ++i) {
		const PluginConfig *m = &s->nodes[effects[i]].dsp[0].config;
		if (channels == 2 && m->input == 1 && m->output == 2)
			return fail(s, "a mono-to-stereo effect requires a mono signal");
		duplicate[i] = channels == 2 && m->input == 1;
		channels = duplicate[i] ? 2 : m->output;
	}
	for (int i = 0; i < count; ++i)
		if (duplicate[i]) {
			Node *n = s->nodes + effects[i];
			if (open_engine(n->dsp + 1, n->path, &n->dsp[0].config, session_rate(s))) {
				for (int j = 0; j <= i; ++j)
					close_engine(s->nodes[effects[j]].dsp + 1);
				return fail(s, "cannot create second mono effect instance");
			}
		}
	Track *t = master ? &s->master : &s->tracks[s->ntracks++];
	*t = (Track){.source = source, .mixer = s->nnodes, .count = count};
	for (int i = 0; i < count; ++i)
		t->effects[i] = effects[i];
	for (int i = 0; i < total; ++i)
		s->nodes[ids[i]].attached = 1;
	Node *m = s->nodes + s->nnodes;
	m->ncontrols = master ? 1 : 2;
	m->attached = 1;
	m->values[0] = 1;
	if (master)
		s->has_master = 1;
	return s->nnodes++;
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
		if (!n->attached)
			return fail(s, "plugin is not connected to a track or master");
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
		if (!n->path)
			continue;
		const PluginConfig *config = &n->dsp[0].config;
		for (int c = 0; c < 2 && n->dsp[c].dsp; ++c) {
			Engine *e = n->dsp + c;
			for (int j = 0; j < config->nparams; ++j)
				if (!(config->outputs & (UINT64_C(1) << j)))
					set_dsp(e->dsp, j, config->defaults[j]);
			reset_dsp(e->dsp);
			e->events = n->events;
			e->count = n->count;
		}
	}
	s->frames = frames;
	s->sealed = 1;
	return 0;
}

static void controls(Node *n, size_t time) {
	while (n->next < n->count && n->events[n->next].time <= time) {
		Event *e = n->events + n->next++;
		n->values[e->parameter] = e->value;
	}
}

static int chain(Session *s, const Track *t, float *audio, int channels, size_t n) {
	float tmp[BLOCK * 2], mono[BLOCK];
	for (int j = 0; j < t->count; ++j) {
		Node *fx = s->nodes + t->effects[j];
		const PluginConfig *m = &fx->dsp[0].config;
		if (fx->dsp[1].dsp) {
			for (int c = 0; c < 2; ++c) {
				for (size_t i = 0; i < n; ++i)
					mono[i] = audio[2 * i + c];
				render(fx->dsp + c, tmp + c * n, mono, n);
			}
			for (size_t i = 0; i < n; ++i) {
				audio[2 * i] = tmp[i];
				audio[2 * i + 1] = tmp[n + i];
			}
		} else {
			if (channels == 1 && m->input == 2)
				for (size_t i = n; i-- > 0;)
					audio[2 * i] = audio[2 * i + 1] = audio[i];
			render(fx->dsp, tmp, audio, n);
			channels = m->output;
			memcpy(audio, tmp, channels * n * sizeof(float));
		}
	}
	return channels;
}

int session_render(Session *s, float *out, size_t frames) {
	if (!s->sealed || frames > s->frames - s->time)
		return fail(s, "render outside session");
	while (frames) {
		size_t n = frames < BLOCK ? frames : BLOCK;
		memset(out, 0, 2 * n * sizeof(float));
		for (int tr = 0; tr < s->ntracks; ++tr) {
			Track *t = s->tracks + tr;
			Node *m = s->nodes + t->mixer;
			float audio[BLOCK * 2];
			Engine *source = s->nodes[t->source].dsp;
			render(source, audio, NULL, n);
			int channels = chain(s, t, audio, source->config.output, n);
			for (size_t i = 0; i < n; ++i) {
				controls(m, s->time + i);
				float pan = m->values[1], gain = m->values[0];
				if (channels == 1) {
					float angle = (pan + 1) * .7853981633974483f, x = audio[i] * gain;
					out[2 * i] += x * cosf(angle);
					out[2 * i + 1] += x * sinf(angle);
				} else {
					out[2 * i] += audio[2 * i] * gain * (pan > 0 ? 1 - pan : 1);
					out[2 * i + 1] += audio[2 * i + 1] * gain * (pan < 0 ? 1 + pan : 1);
				}
			}
		}
		if (s->has_master) {
			if (chain(s, &s->master, out, 2, n) == 1)
				for (size_t i = n; i-- > 0;)
					out[2 * i] = out[2 * i + 1] = out[i];
			Node *m = s->nodes + s->master.mixer;
			for (size_t i = 0; i < n; ++i) {
				controls(m, s->time + i);
				out[2 * i] *= m->values[0];
				out[2 * i + 1] *= m->values[0];
			}
		}
		for (size_t i = 0; i < 2 * n; ++i)
			if (!isfinite(out[i]))
				return fail(s, "non-finite audio");
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
	}
	*s = (Session){0};
}
