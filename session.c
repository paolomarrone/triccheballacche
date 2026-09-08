#include "session.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int fail(Session *s, const char *message) { s->error = message; return -1; }
static const tibia_parameter mixer_params[] = {
	{"gain", "linear", 0, 4, 1, 0}, {"pan", "", -1, 1, 0, 0}
};
static const tibia_info mixer_info = {0, 0, 2, mixer_params}, master_info = {0, 0, 1, mixer_params};
static int valid(Session *s, int id, int param, float value) {
	if (s->sealed || id < 0 || id >= s->nnodes) return fail(s, "sealed session or invalid handle");
	const tibia_info *info = s->nodes[id].info;
	if (param < 0 || (size_t)param >= info->count) return fail(s, "unknown parameter");
	const tibia_parameter *p = info->parameters + param;
	if (!isfinite(value) || value < p->minimum || value > p->maximum || (p->integer && value != floorf(value)))
		return fail(s, "parameter outside range");
	return 0;
}
int session_plugin(Session *s, const char *path) {
	if (s->sealed || s->nnodes == MAX_NODES) return fail(s, "sealed session or node limit reached");
	Node *n = s->nodes + s->nnodes;
	*n = (Node){0};
	if (open_engine(n->dsp, path)) { close_engine(n->dsp); return fail(s, "cannot open plugin"); }
	n->info = n->dsp[0].module->info;
	if (!n->info || n->info->count > MAX_PARAMS || (n->info->count && !n->info->parameters)
		|| (n->info->input != 0 && n->info->input != 1)) {
		close_engine(n->dsp); return fail(s, "plugin needs valid mono metadata (tibia_get_info)");
	}
	for (size_t i = 0; i < n->info->count; ++i) {
		const tibia_parameter *p = n->info->parameters + i;
		int bad = !p->name || !*p->name || !p->unit || !isfinite(p->minimum) || !isfinite(p->maximum) ||
			!isfinite(p->default_value) || p->minimum > p->maximum || p->default_value < p->minimum ||
			p->default_value > p->maximum || (p->integer && floorf(p->default_value) != p->default_value);
		for (size_t j = 0; !bad && j < i; ++j) bad |= !strcmp(p->name, n->info->parameters[j].name);
		if (bad) { close_engine(n->dsp); return fail(s, "invalid parameter metadata"); }
	}
	n->path = malloc(strlen(path) + 1);
	if (!n->path) { close_engine(n->dsp); return fail(s, "out of memory"); }
	strcpy(n->path, path);
	for (size_t i = 0; i < n->info->count; ++i) n->initial[i] = n->values[i] = n->info->parameters[i].default_value;
	return s->nnodes++;
}
int session_set(Session *s, int id, int param, float value) {
	if (valid(s, id, param, value)) return -1;
	s->nodes[id].initial[param] = s->nodes[id].values[param] = value;
	return 0;
}
int session_track(Session *s, int source, const int *effects, int count, int master) {
	if (s->sealed || s->nnodes == MAX_NODES || count < 0 || count > MAX_FX ||
		(master ? s->has_master : s->ntracks == MAX_TRACKS)) return fail(s, "sealed session or track/effect limit reached");
	int ids[MAX_FX + 1], total = 0;
	if (!master) ids[total++] = source;
	for (int i = 0; i < count; ++i) ids[total++] = effects[i];
	for (int i = 0; i < total; ++i) {
		int id = ids[i];
		if (id < 0 || id >= s->nnodes || !s->nodes[id].path || s->nodes[id].attached ||
			s->nodes[id].info->input != (master || i > 0)) return fail(s, "expected unused source/effect plugin");
		for (int j = 0; j < i; ++j) if (ids[j] == id) return fail(s, "plugin already in this chain");
	}
	if (master) for (int i = 0; i < count; ++i) {
		Node *n = s->nodes + effects[i];
		if (open_engine(n->dsp + 1, n->path)) {
			for (int j = 0; j <= i; ++j) close_engine(s->nodes[effects[j]].dsp + 1);
			return fail(s, "cannot create stereo master effect");
		}
	}
	Track *t = master ? &s->master : &s->tracks[s->ntracks++];
	*t = (Track){.source = source, .mixer = s->nnodes, .count = count};
	for (int i = 0; i < count; ++i) t->effects[i] = effects[i];
	for (int i = 0; i < total; ++i) s->nodes[ids[i]].attached = 1;
	Node *m = s->nodes + s->nnodes;
	m->info = master ? &master_info : &mixer_info;
	m->attached = 1; m->initial[0] = m->values[0] = 1;
	if (master) s->has_master = 1;
	return s->nnodes++;
}
static int reserve(Session *s, Node *n, size_t extra) {
	if (extra > SIZE_MAX - n->count) return fail(s, "too many events");
	size_t need = n->count + extra;
	if (need <= n->capacity) return 0;
	size_t cap = n->capacity ? n->capacity * 2 : 128;
	if (cap < need) cap = need;
	if (cap > SIZE_MAX / sizeof(Event)) return fail(s, "too many events");
	Event *events = realloc(n->events, cap * sizeof(*events));
	if (!events) return fail(s, "out of memory");
	n->events = events; n->capacity = cap;
	return 0;
}
int session_param(Session *s, int id, size_t time, int param, float value) {
	if (valid(s, id, param, value)) return -1;
	Node *n = s->nodes + id;
	if (reserve(s, n, 1)) return -1;
	n->events[n->count] = (Event){time, param, value, {0}, n->count}; ++n->count;
	return 0;
}
int session_note(Session *s, int id, size_t time, size_t end, int pitch, int velocity) {
	if (s->sealed || id < 0 || id >= s->nnodes || end <= time || pitch < 0 || pitch > 127 || velocity < 1 || velocity > 127)
		return fail(s, "invalid note or sealed session");
	Node *n = s->nodes + id;
	if (!n->path || !n->info->midi || !n->dsp[0].module->midi_msg_in) return fail(s, "node has no MIDI input");
	if (reserve(s, n, 2)) return -1;
	n->events[n->count] = (Event){time, -1, 0, {0x90, pitch, velocity}, n->count}; ++n->count;
	n->events[n->count] = (Event){end, -1, 0, {0x80, pitch, 0}, n->count}; ++n->count;
	return 0;
}
static int compare(const void *aa, const void *bb) {
	const Event *a = aa, *b = bb;
	if (a->time != b->time) return a->time < b->time ? -1 : 1;
	int pa = a->parameter >= 0 ? 0 : a->midi[0], pb = b->parameter >= 0 ? 0 : b->midi[0];
	if (pa != pb) return pa < pb ? -1 : 1;
	return (a->order > b->order) - (a->order < b->order);
}
int session_end(Session *s, size_t frames) {
	if (s->sealed || !frames) return fail(s, "empty duration or already sealed");
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		if (!n->attached) return fail(s, "plugin is not connected to a track or master");
		for (size_t j = 0; j < n->count; ++j) {
			Event *e = n->events + j;
			if (e->time > frames || (e->time == frames && (e->parameter >= 0 || e->midi[0] != 0x80)))
				return fail(s, "event outside duration");
		}
	}
	for (int i = 0; i < s->nnodes; ++i) {
		Node *n = s->nodes + i;
		if (n->count) qsort(n->events, n->count, sizeof(Event), compare);
		for (int c = 0; c < 2 && n->dsp[c].instance; ++c) {
			Engine *e = n->dsp + c;
			for (size_t j = 0; j < n->info->count; ++j) e->module->set_parameter(e->instance, j, n->initial[j]);
			e->module->reset(e->instance); e->events = n->events; e->count = n->count;
		}
	}
	s->frames = frames; s->sealed = 1;
	return 0;
}
static void controls(Node *n, size_t time) {
	while (n->next < n->count && n->events[n->next].time <= time) {
		Event *e = n->events + n->next++;
		n->values[e->parameter] = e->value;
	}
}
static void chain(Session *s, const Track *t, float *audio, int channel, size_t n) {
	float tmp[BLOCK];
	for (int j = 0; j < t->count; ++j) {
		render(s->nodes[t->effects[j]].dsp + channel, tmp, audio, n);
		memcpy(audio, tmp, n * sizeof(float));
	}
}
int session_render(Session *s, float *out, size_t frames) {
	if (!s->sealed || frames > s->frames - s->time) return fail(s, "render outside session");
	while (frames) {
		size_t n = frames < BLOCK ? frames : BLOCK;
		memset(out, 0, 2 * n * sizeof(float));
		for (int tr = 0; tr < s->ntracks; ++tr) {
			Track *t = s->tracks + tr; Node *m = s->nodes + t->mixer;
			float audio[BLOCK];
			render(s->nodes[t->source].dsp, audio, NULL, n); chain(s, t, audio, 0, n);
			for (size_t i = 0; i < n; ++i) {
				controls(m, s->time + i);
				float angle = (m->values[1] + 1) * .7853981633974483f, x = audio[i] * m->values[0];
				out[2 * i] += x * cosf(angle); out[2 * i + 1] += x * sinf(angle);
			}
		}
		if (s->has_master) {
			for (int c = 0; c < 2; ++c) {
				float audio[BLOCK];
				for (size_t i = 0; i < n; ++i) audio[i] = out[2 * i + c];
				chain(s, &s->master, audio, c, n);
				for (size_t i = 0; i < n; ++i) out[2 * i + c] = audio[i];
			}
			Node *m = s->nodes + s->master.mixer;
			for (size_t i = 0; i < n; ++i) {
				controls(m, s->time + i);
				out[2 * i] *= m->values[0]; out[2 * i + 1] *= m->values[0];
			}
		}
		for (size_t i = 0; i < 2 * n; ++i) if (!isfinite(out[i])) return fail(s, "non-finite audio");
		s->time += n; frames -= n; out += 2 * n;
	}
	return 0;
}
void session_pop(Session *s) {
	Node *n = s->nodes + --s->nnodes;
	close_engine(n->dsp); close_engine(n->dsp + 1); free(n->events); free(n->path);
	*n = (Node){0};
}
void session_free(Session *s) {
	while (s->nnodes) session_pop(s);
	*s = (Session){0};
}
