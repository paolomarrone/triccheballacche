#include "score_view.h"
#include "util.h"
#include <math.h>

void score_view_free(ScoreView *view) {
	for (int i = 0; i < view->nnodes; ++i) {
		free(view->nodes[i].name);
		free(view->nodes[i].label);
		free(view->nodes[i].bundle);
		free(view->nodes[i].product);
		free(view->nodes[i].events);
		free(view->nodes[i].by_order);
	}
	for (size_t i = 0; i < view->norigins; ++i) {
		for (size_t j = 0; j < view->origins[i].count; ++j)
			free(view->origins[i].frames[j].file);
		free(view->origins[i].frames);
	}
	free(view->origins);
	free(view->references);
	*view = (ScoreView){0};
}

int score_view_init(ScoreView *view, const Session *session) {
	*view = (ScoreView){.nnodes = session->nnodes,
	    .ntracks = session->ntracks,
	    .output = session->output,
	    .repeating = session->sequence != NULL,
	    .end = session->frames == UINT64_MAX ? 0 : (double)session->frames / session->sample_rate};
	memcpy(view->tracks, session->tracks, session->ntracks * sizeof(Track));
	// A serial processing path keeps the original source's notes. Stop at another
	// track or a sum: group rows do not duplicate the notes of their input tracks.
	for (int i = 0; i < view->ntracks; ++i) {
		int *id = &view->tracks[i].source;
		while (!session->nodes[*id].track && session->nodes[*id].ninputs == 1)
			*id = session->nodes[*id].inputs[0].node;
	}
	if (session->has_master) {
		view->tracks[view->ntracks] = session->master;
		view->tracks[view->ntracks++].source = -1;
	}
	for (int i = 0; i < session->nnodes; ++i) {
		const Node *source = session->nodes + i;
		ScoreNode *node = view->nodes + i;
		node->ninputs = source->ninputs;
		for (int j = 0; j < source->ninputs; ++j)
			node->inputs[j] = source->inputs[j].node;
		node->upstream = source->upstream;
		node->downstream = source->downstream;
		if (source->name && !(node->label = copy_string(source->name)))
			return -1;
		const char *name = source->path ? strrchr(source->path, '/') : NULL;
		node->name = copy_string(name ? name + 1 : source->path ? source->path : "Mixer");
		if (source->path) {
			node->bundle = copy_string(source->path);
			if (!node->bundle)
				return -1;
			// Perone binaries occupy exactly one platform directory inside the bundle.
			for (int j = 0; j < 2; ++j) {
				char *slash = strrchr(node->bundle, '/');
				if (!slash)
					return -1;
				*slash = 0;
			}
		}
		if (session->sequence) {
			const Sequence *sequence = session->sequence;
			for (size_t j = 0; j < sequence->count; ++j)
				node->count += sequence->cues[j].node == i;
			node->raw_count = node->count;
			node->events = calloc(node->count ? node->count : 1, sizeof(ScoreEvent));
			if (!node->name || !node->events)
				return -1;
			size_t k = 0;
			for (size_t j = 0; j < sequence->count; ++j) {
				const Cue *c = sequence->cues + j;
				if (c->node != i)
					continue;
				node->events[k] = (ScoreEvent){.start = c->start,
				    .end = c->end,
				    .period = c->period,
				    .order = k,
				    .pitch = c->parameter < 0 ? c->pitch : -1,
				    .velocity = c->velocity};
				++k;
			}
			continue;
		}
		node->raw_count = node->count = source->count;
		if (!node->name || source->count > SIZE_MAX / sizeof(ScoreEvent))
			return -1;
		if (!source->count)
			continue;
		node->events = calloc(source->count, sizeof(ScoreEvent));
		if (!node->events)
			return -1;
		for (size_t j = 0; j < source->count; ++j) {
			const Event *event = source->events + j;
			ScoreEvent *out = node->events + event->order;
			*out = (ScoreEvent){.start = (double)event->time / session->sample_rate,
			    .order = event->order,
			    .pitch = event->parameter >= 0 ? -1
			        : event->midi[0] == 0x90   ? event->midi[1]
			                                   : -2,
			    .velocity = event->midi[2]};
			out->end = out->start;
			// session_note emits adjacent on/off orders even for overlapping notes of the same pitch.
			if (out->pitch == -2)
				node->events[event->order - 1].end = out->start;
		}
	}
	return 0;
}

static int compare(const void *aa, const void *bb) {
	const ScoreEvent *a = aa, *b = bb;
	if (a->start != b->start)
		return a->start < b->start ? -1 : 1;
	return (a->order > b->order) - (a->order < b->order);
}

static void merge(ScoreSummary *out, ScoreSummary other) {
	if (!other.count)
		return;
	if (!out->count || other.low < out->low)
		out->low = other.low;
	if (!out->count || other.high > out->high)
		out->high = other.high;
	out->count += other.count;
}

// The sorted array is also a balanced interval tree; no per-event heap nodes.
static void index_events(ScoreEvent *events, size_t lo, size_t hi) {
	if (lo == hi)
		return;
	size_t mid = lo + (hi - lo) / 2;
	ScoreEvent *event = events + mid;
	event->max_end = fmax(event->end, event->start + 0.08);
	event->summary = (ScoreSummary){event->pitch >= 0, event->pitch, event->pitch};
	event->min_note_end = event->pitch >= 0 ? event->end : INFINITY;
	index_events(events, lo, mid);
	index_events(events, mid + 1, hi);
	if (lo < mid) {
		const ScoreEvent *left = events + lo + (mid - lo) / 2;
		event->max_end = fmax(event->max_end, left->max_end);
		merge(&event->summary, left->summary);
		event->min_note_end = fmin(event->min_note_end, left->min_note_end);
	}
	if (mid + 1 < hi) {
		const ScoreEvent *right = events + mid + 1 + (hi - mid - 1) / 2;
		event->max_end = fmax(event->max_end, right->max_end);
		merge(&event->summary, right->summary);
		event->min_note_end = fmin(event->min_note_end, right->min_note_end);
	}
}

int score_view_index(ScoreView *view) {
	for (int i = 0; i < view->nnodes; ++i) {
		ScoreNode *node = view->nodes + i;
		size_t count = 0;
		for (size_t j = 0; j < node->count; ++j)
			if (node->events[j].pitch >= -1)
				node->events[count++] = node->events[j];
		node->count = count;
		if (!count)
			continue;
		qsort(node->events, count, sizeof(ScoreEvent), compare);
		node->by_order = malloc(node->raw_count * sizeof(size_t));
		if (!node->by_order)
			return -1;
		for (size_t j = 0; j < node->raw_count; ++j)
			node->by_order[j] = SIZE_MAX;
		for (size_t j = 0; j < count; ++j)
			node->by_order[node->events[j].order] = j;
		index_events(node->events, 0, count);
	}
	return 0;
}

static int visit_events(const ScoreEvent *events, size_t lo, size_t hi, double from, double to, int notes,
    int (*visit)(const ScoreEvent *, void *), void *context) {
	if (lo == hi || events[lo].start >= to)
		return 1;
	size_t mid = lo + (hi - lo) / 2;
	const ScoreEvent *event = events + mid;
	if (event->max_end <= from || (notes && !event->summary.count))
		return 1;
	if (!visit_events(events, lo, mid, from, to, notes, visit, context))
		return 0;
	double end = notes ? event->end : fmax(event->end, event->start + 0.08);
	if (event->start < to && end > from && (!notes || event->pitch >= 0) && !visit(event, context))
		return 0;
	return visit_events(events, mid + 1, hi, from, to, notes, visit, context);
}

// First representable cycle whose endpoint is >= time (inclusive), or > time.
// Correct the estimate against actual endpoints; division can round across a boundary.
static double first_cycle(double start, double period, double time, int inclusive) {
	double limit = 0x1p52, k = fmax(0, fmin(limit, floor((time - start) / period)));
	while (k > 0 && (inclusive ? start + (k - 1) * period >= time : start + (k - 1) * period > time))
		--k;
	while (k < limit && (inclusive ? start + k * period < time : start + k * period <= time))
		++k;
	return k;
}

// Count occurrences analytically, so a wide viewport never expands a long performance.
static void occurrences(const ScoreEvent *e, double from, double to, int notes, double *first, double *last) {
	double end = notes ? e->end : fmax(e->end, e->start + .08);
	if (e->period) {
		*first = fmax(first_cycle(end, e->period, from, 0), first_cycle(e->start, e->period, 0, 1));
		*last = fmax(*first, first_cycle(e->start, e->period, to, 1));
	} else {
		*first = 0;
		*last = e->start >= 0 && e->start < to && end > from;
	}
}

void score_view_visit(const ScoreView *view, int node, double from, double to, int notes_only,
    int (*visit)(const ScoreEvent *, void *), void *context) {
	if (node < 0 || node >= view->nnodes || !isfinite(from) || !isfinite(to) || from >= to)
		return;
	if (view->repeating) {
		const ScoreNode *n = view->nodes + node;
		if (view->end > 0)
			to = fmin(to, view->end);
		if (from >= to)
			return;
		for (size_t i = 0; i < n->count; ++i) {
			const ScoreEvent *e = n->events + i;
			if (notes_only && e->pitch < 0)
				continue;
			double first, last;
			occurrences(e, from, to, notes_only, &first, &last);
			if (!notes_only) {
				if (e->period)
					first = fmax(first, first_cycle(e->start, e->period, view->active_from, 1));
				else if (e->start < view->active_from)
					continue;
			}
			for (double k = first; k < last; ++k) {
				ScoreEvent occurrence = *e;
				occurrence.start += k * e->period;
				occurrence.end += k * e->period;
				if (!visit(&occurrence, context))
					return;
			}
		}
		return;
	}
	visit_events(view->nodes[node].events, 0, view->nodes[node].count, from, to, notes_only, visit, context);
}

static ScoreSummary summarize(const ScoreEvent *events, size_t lo, size_t hi, double from, double to) {
	ScoreSummary out = {0};
	if (lo == hi || events[lo].start >= to)
		return out;
	size_t mid = lo + (hi - lo) / 2;
	const ScoreEvent *event = events + mid;
	if (event->max_end <= from || !event->summary.count)
		return out;
	if (event->min_note_end > from && events[hi - 1].start < to)
		return event->summary;
	out = summarize(events, lo, mid, from, to);
	if (event->pitch >= 0 && event->start < to && event->end > from)
		merge(&out, (ScoreSummary){1, event->pitch, event->pitch});
	merge(&out, summarize(events, mid + 1, hi, from, to));
	return out;
}

ScoreSummary score_view_summary(const ScoreView *view, int node, double from, double to) {
	if (node < 0 || node >= view->nnodes || !isfinite(from) || !isfinite(to) || from >= to)
		return (ScoreSummary){0};
	const ScoreNode *n = view->nodes + node;
	if (!view->repeating)
		return summarize(n->events, 0, n->count, from, to);
	if (view->end > 0)
		to = fmin(to, view->end);
	ScoreSummary result = {0};
	if (from >= to)
		return result;
	for (size_t i = 0; i < n->count; ++i) {
		const ScoreEvent *e = n->events + i;
		if (e->pitch < 0)
			continue;
		double first, last;
		occurrences(e, from, to, 1, &first, &last);
		size_t room = SIZE_MAX - result.count;
		size_t count = last - first >= (double)room ? room : (size_t)(last - first);
		merge(&result, (ScoreSummary){count, e->pitch, e->pitch});
	}
	return result;
}

const ScoreEvent *score_view_find(const ScoreView *view, int node, uint64_t order) {
	if (node < 0 || node >= view->nnodes)
		return NULL;
	const ScoreNode *n = view->nodes + node;
	return n->by_order && order < n->raw_count && n->by_order[order] != SIZE_MAX ? n->events + n->by_order[order]
	                                                                             : NULL;
}

void score_view_remap(ScoreView *view, const int *mapping) {
	ScoreNode nodes[MAX_NODES];
	memcpy(nodes, view->nodes, view->nnodes * sizeof(ScoreNode));
	for (int i = 0; i < view->nnodes; ++i) {
		ScoreNode *n = view->nodes + mapping[i];
		*n = nodes[i];
		for (int j = 0; j < n->ninputs; ++j)
			n->inputs[j] = mapping[n->inputs[j]];
	}
	view->output = mapping[view->output];
	for (int i = 0; i < view->ntracks; ++i) {
		Track *t = view->tracks + i;
		if (t->source >= 0)
			t->source = mapping[t->source];
		t->mixer = mapping[t->mixer];
		for (int j = 0; j < t->count; ++j)
			t->effects[j] = mapping[t->effects[j]];
	}
}

void score_view_activate(ScoreView *view, const Session *session) {
	const Sequence *sequence = atomic_load(&session->sequence);
	view->active_from = sequence && sequence->at ? ((double)sequence->at - .5) / session->sample_rate : 0;
}
