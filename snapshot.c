#include "snapshot.h"
#include <stdlib.h>
#include <string.h>

#define SNAPSHOT_LIMIT (256u * 1024u * 1024u)

typedef struct {
	unsigned char *data;
	size_t length, position, capacity;
	int reading, failed;
} Transfer;

static void bytes(Transfer *t, void *value, size_t length) {
	if (t->failed || length > SNAPSHOT_LIMIT || t->position > SNAPSHOT_LIMIT - length) {
		t->failed = 1;
		return;
	}
	size_t end = t->position + length;
	if (t->reading) {
		if (end > t->length) {
			t->failed = 1;
			return;
		}
		if (length)
			memcpy(value, t->data + t->position, length);
	} else {
		if (end > t->capacity) {
			size_t capacity = end < 2 * t->capacity ? 2 * t->capacity : end;
			unsigned char *data = realloc(t->data, capacity);
			if (!data) {
				t->failed = 1;
				return;
			}
			t->data = data;
			t->capacity = capacity;
		}
		if (length)
			memcpy(t->data + t->position, value, length);
	}
	t->position = end;
}

static void *array(Transfer *t, void *value, size_t count, size_t width) {
	if (t->failed)
		return value;
	if (count > SNAPSHOT_LIMIT / width) {
		t->failed = 1;
		return t->reading ? NULL : value;
	}
	if (t->reading) {
		value = count ? calloc(count, width) : NULL;
		if (count && !value) {
			t->failed = 1;
			return NULL;
		}
	}
	bytes(t, value, count * width);
	return value;
}

static void string(Transfer *t, char **value) {
	size_t length = !t->reading && *value ? strlen(*value) + 1 : 0;
	bytes(t, &length, sizeof(length));
	void *data = array(t, *value, length, 1);
	if (t->reading)
		*value = data;
	if (t->reading && length && !t->failed && (*value)[length - 1])
		t->failed = 1;
}

#define FIELD(t, field) bytes(t, &(field), sizeof(field))

static void transfer(Transfer *t, Session *s, Output *output, ScoreView *view) {
	uint32_t magic = UINT32_C(0x54424331);
	FIELD(t, magic);
	if (magic != UINT32_C(0x54424331)) {
		t->failed = 1;
		return;
	}
	FIELD(t, s->sample_rate);
	FIELD(t, s->frames);
	FIELD(t, *output);
	FIELD(t, s->nnodes);
	FIELD(t, s->ntracks);
	FIELD(t, s->has_master);
	FIELD(t, s->output);
	if (t->failed || s->nnodes < 1 || s->nnodes > MAX_NODES || s->ntracks < 0 || s->ntracks > MAX_TRACKS ||
	    s->output < 0 || s->output >= s->nnodes || s->has_master < 0 || s->has_master > 1) {
		s->nnodes = s->ntracks = 0;
		t->failed = 1;
		return;
	}
	for (int i = 0; i < s->nnodes && !t->failed; ++i) {
		Node *n = s->nodes + i;
		string(t, &n->path);
		string(t, &n->name);
		string(t, &n->key);
		FIELD(t, n->dsp[0].config);
		FIELD(t, n->defaults);
		FIELD(t, n->ncontrols);
		FIELD(t, n->track);
		FIELD(t, n->ninputs);
		FIELD(t, n->count);
		if (n->ninputs < 0 || n->ninputs > MAX_NODES || n->track < 0 || n->track > s->ntracks) {
			t->failed = 1;
			return;
		}
		n->inputs = array(t, n->inputs, n->ninputs, sizeof(Input));
		n->events = array(t, n->events, n->count, sizeof(Event));
		for (int j = 0; j < n->ninputs && !t->failed; ++j)
			if (n->inputs[j].node < 0 || n->inputs[j].node >= s->nnodes)
				t->failed = 1;
		if (t->reading)
			n->capacity = n->count;
	}
	bytes(t, s->tracks, s->ntracks * sizeof(Track));
	FIELD(t, s->master);
	int sequence = s->sequence != NULL;
	FIELD(t, sequence);
	if (sequence) {
		if (t->reading && !(s->sequence = calloc(1, sizeof(Sequence)))) {
			t->failed = 1;
			return;
		}
		Sequence *q = s->sequence;
		FIELD(t, q->count);
		FIELD(t, q->bpm);
		FIELD(t, q->quantum);
		void *cues = array(t, q->cues, q->count, sizeof(Cue));
		if (t->reading) {
			q->cues = cues;
			q->capacity = q->count;
		}
	}
	FIELD(t, view->nnodes);
	FIELD(t, view->ntracks);
	FIELD(t, view->output);
	FIELD(t, view->end);
	FIELD(t, view->active_from);
	FIELD(t, view->repeating);
	if (view->nnodes != s->nnodes || view->ntracks < 0 || view->ntracks > MAX_TRACKS + 1) {
		view->nnodes = 0;
		t->failed = 1;
		return;
	}
	bytes(t, view->tracks, view->ntracks * sizeof(Track));
	for (int i = 0; i < view->nnodes && !t->failed; ++i) {
		ScoreNode *n = view->nodes + i;
		FIELD(t, n->inputs);
		FIELD(t, n->ninputs);
		FIELD(t, n->upstream);
		FIELD(t, n->downstream);
		FIELD(t, n->minimum);
		FIELD(t, n->maximum);
		FIELD(t, n->integers);
		string(t, &n->label);
		string(t, &n->name);
		string(t, &n->bundle);
		string(t, &n->product);
		FIELD(t, n->count);
		FIELD(t, n->raw_count);
		n->events = array(t, n->events, n->count, sizeof(ScoreEvent));
		n->by_order = array(t, n->by_order, n->raw_count, sizeof(size_t));
	}
	FIELD(t, view->norigins);
	FIELD(t, view->nreferences);
	if (view->norigins > SNAPSHOT_LIMIT / sizeof(ScoreOrigin)) {
		if (t->reading)
			view->norigins = 0;
		t->failed = 1;
		return;
	}
	if (t->reading && view->norigins && !(view->origins = calloc(view->norigins, sizeof(ScoreOrigin)))) {
		view->norigins = 0;
		t->failed = 1;
		return;
	}
	for (size_t i = 0; i < view->norigins && !t->failed; ++i) {
		ScoreOrigin *o = view->origins + i;
		FIELD(t, o->count);
		if (o->count > SNAPSHOT_LIMIT / sizeof(ScoreFrame)) {
			o->count = 0;
			t->failed = 1;
			return;
		}
		if (t->reading && o->count && !(o->frames = calloc(o->count, sizeof(ScoreFrame)))) {
			o->count = 0;
			t->failed = 1;
			return;
		}
		for (size_t j = 0; j < o->count && !t->failed; ++j) {
			string(t, &o->frames[j].file);
			FIELD(t, o->frames[j].line);
			FIELD(t, o->frames[j].column);
		}
	}
	view->references = array(t, view->references, view->nreferences, sizeof(size_t));
}

void *score_pack(const Session *s, const Output *output, const ScoreView *view, size_t *length) {
	Transfer t = {0};
	Session description = *s;
	ScoreView projection = *view;
	Output config = *output;
	transfer(&t, &description, &config, &projection);
	if (t.failed) {
		free(t.data);
		return NULL;
	}
	*length = t.position;
	return t.data;
}

int score_unpack(Session *s, Output *output, ScoreView *view, const void *data, size_t length) {
	Transfer t = {.data = (unsigned char *)data, .length = length, .reading = 1};
	if (s->nnodes || view->nnodes || length > SNAPSHOT_LIMIT)
		return -1;
	s->describe = 1;
	transfer(&t, s, output, view);
	if (t.failed || t.position != length)
		return -1;
	s->has_output = 1;
	return session_end(s, s->frames);
}
