#include "score_view.h"
#include "util.h"
#include <math.h>

void score_view_free(ScoreView *view) {
	for (int i = 0; i < view->nnodes; ++i) {
		free(view->nodes[i].name);
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
	*view = (ScoreView){
	    .nnodes = session->nnodes, .ntracks = session->ntracks, .end = (double)session->frames / session->sample_rate};
	memcpy(view->tracks, session->tracks, session->ntracks * sizeof(Track));
	if (session->has_master)
		view->tracks[view->ntracks++] = session->master;
	for (int i = 0; i < session->nnodes; ++i) {
		const Node *source = session->nodes + i;
		ScoreNode *node = view->nodes + i;
		const char *name = source->path ? strrchr(source->path, '/') : NULL;
		node->name = copy_string(name ? name + 1 : source->path ? source->path : "Mixer");
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

void score_view_visit(const ScoreView *view, int node, double from, double to, int notes_only,
    int (*visit)(const ScoreEvent *, void *), void *context) {
	if (node >= 0 && node < view->nnodes && isfinite(from) && isfinite(to) && from < to)
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
	return summarize(n->events, 0, n->count, from, to);
}

const ScoreEvent *score_view_find(const ScoreView *view, int node, size_t order) {
	if (node < 0 || node >= view->nnodes)
		return NULL;
	const ScoreNode *n = view->nodes + node;
	return n->by_order && order < n->raw_count && n->by_order[order] != SIZE_MAX ? n->events + n->by_order[order]
	                                                                             : NULL;
}
