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
		free(view->nodes[i].controls);
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

int score_view_init(ScoreView *view, const Score *score) {
	*view = (ScoreView){.nnodes = score->nnodes,
	    .ntracks = score->ntracks,
	    .output = score->output,
	    .repeating = 0,
	    .sample_rate = score->sample_rate,
	    .end = score->frames == UINT64_MAX ? 0 : (double)score->frames / score->sample_rate};
	const Sequence *sequence = &score->sequence;
	for (size_t i = 0; i < sequence->count; ++i)
		++view->nodes[sequence->cues[i].node].count;
	memcpy(view->tracks, score->tracks, score->ntracks * sizeof(Track));
	// A serial processing path keeps the original source's notes. Stop at another
	// track or a sum: group rows do not duplicate the notes of their input tracks.
	for (int i = 0; i < view->ntracks; ++i) {
		int *id = &view->tracks[i].source;
		while (!score->nodes[*id].track && score->nodes[*id].ninputs == 1)
			*id = score->nodes[*id].inputs[0];
	}
	if (score->has_master) {
		view->tracks[view->ntracks] = score->master;
		view->tracks[view->ntracks++].source = -1;
	}
	for (int i = 0; i < score->nnodes; ++i) {
		const Node *source = score->nodes + i;
		ScoreNode *node = view->nodes + i;
		memcpy(node->defaults, source->config.defaults, source->config.nparams * sizeof(float));
		memcpy(node->minimum, source->info.minimum, sizeof(node->minimum));
		memcpy(node->maximum, source->info.maximum, sizeof(node->maximum));
		node->integers = source->info.integers;
		if (source->info.product && !(node->product = copy_string(source->info.product)))
			return -1;
		node->ninputs = source->ninputs;
		for (int j = 0; j < source->ninputs; ++j)
			node->inputs[j] = source->inputs[j];
		node->upstream = source->upstream;
		node->downstream = source->downstream;
		if (source->name && !(node->label = copy_string(source->name)))
			return -1;
		const char *name = source->path ? strrchr(source->path, '/') : NULL;
		node->name = copy_string(source->info.name ? source->info.name
		        : name                             ? name + 1
		        : source->path                     ? source->path
		                                           : "Mixer");
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
		if (node->count && !(node->events = calloc(node->count, sizeof(ScoreEvent))))
			return -1;
		if (!node->name)
			return -1;
		node->count = 0;
	}
	for (size_t i = 0; i < sequence->count; ++i) {
		const Cue *c = sequence->cues + i;
		ScoreNode *node = view->nodes + c->node;
		node->events[node->count] = (ScoreEvent){.start = c->start,
		    .end = c->end,
		    .period = c->period,
		    .order = node->count,
		    .pitch = c->parameter < 0 ? c->pitch : -1,
		    .velocity = c->velocity,
		    .parameter = c->parameter,
		    .stream = c->stream,
		    .value = c->value};
		++node->count;
		view->repeating |= c->period > 0;
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
	event->summary = (ScoreSummary){event->pitch >= 0 && event->start >= 0, event->pitch, event->pitch};
	event->min_note_end = event->summary.count ? event->end : INFINITY;
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
		free(node->by_order);
		free(node->controls);
		node->by_order = node->controls = NULL;
		memset(node->offsets, 0, sizeof(node->offsets));
		size_t count = node->count;
		if (!count)
			continue;
		qsort(node->events, count, sizeof(ScoreEvent), compare);
		node->by_order = malloc(node->count * sizeof(size_t));
		if (!node->by_order)
			return -1;
		for (size_t j = 0; j < node->count; ++j)
			node->by_order[j] = SIZE_MAX;
		for (size_t j = 0; j < count; ++j)
			node->by_order[node->events[j].order] = j;
		for (size_t j = 0; j < count; ++j)
			if (node->events[j].pitch == -1)
				++node->offsets[node->events[j].parameter + 1];
		for (int j = 1; j <= MAX_PARAMS; ++j)
			node->offsets[j] += node->offsets[j - 1];
		if (node->offsets[MAX_PARAMS]) {
			node->controls = malloc(node->offsets[MAX_PARAMS] * sizeof(size_t));
			if (!node->controls)
				return -1;
			size_t next[MAX_PARAMS];
			memcpy(next, node->offsets, sizeof(next));
			for (size_t j = 0; j < count; ++j)
				if (node->events[j].pitch == -1)
					node->controls[next[node->events[j].parameter]++] = j;
		}
		index_events(node->events, 0, count);
	}
	return 0;
}

static int visit_events(const ScoreEvent *events, size_t lo, size_t hi, double from, double to, double minimum,
    int notes, int (*visit)(const ScoreEvent *, void *), void *context) {
	if (lo == hi || events[lo].start >= to || events[hi - 1].start < minimum)
		return 1;
	size_t mid = lo + (hi - lo) / 2;
	const ScoreEvent *event = events + mid;
	if (event->max_end <= from || (notes && !event->summary.count))
		return 1;
	if (!visit_events(events, lo, mid, from, to, minimum, notes, visit, context))
		return 0;
	double end = notes ? event->end : fmax(event->end, event->start + 0.08);
	if (event->start >= minimum && event->start < to && end > from && (!notes || event->pitch >= 0) &&
	    !visit(event, context))
		return 0;
	return visit_events(events, mid + 1, hi, from, to, minimum, notes, visit, context);
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
	if (view->end > 0)
		to = fmin(to, view->end);
	if (from >= to)
		return;
	if (view->repeating) {
		const ScoreNode *n = view->nodes + node;
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
	visit_events(view->nodes[node].events, 0, view->nodes[node].count, from, to, notes_only ? 0 : view->active_from,
	    notes_only, visit, context);
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
	if (event->pitch >= 0 && event->start >= 0 && event->start < to && event->end > from)
		merge(&out, (ScoreSummary){1, event->pitch, event->pitch});
	merge(&out, summarize(events, mid + 1, hi, from, to));
	return out;
}

ScoreSummary score_view_summary(const ScoreView *view, int node, double from, double to) {
	if (node < 0 || node >= view->nnodes || !isfinite(from) || !isfinite(to) || from >= to)
		return (ScoreSummary){0};
	const ScoreNode *n = view->nodes + node;
	if (view->end > 0)
		to = fmin(to, view->end);
	ScoreSummary result = {0};
	if (from >= to)
		return result;
	if (!view->repeating)
		return summarize(n->events, 0, n->count, from, to);
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
	return n->by_order && order < n->count && n->by_order[order] != SIZE_MAX ? n->events + n->by_order[order] : NULL;
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

void score_view_activate(ScoreView *view, uint64_t at) {
	view->active_from = at ? ((double)at - .5) / view->sample_rate : 0;
}
