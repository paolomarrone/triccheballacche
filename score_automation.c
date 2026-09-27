#include "score_view.h"
#include <math.h>
#include <stdlib.h>

static ScoreControl occurrence(const ScoreView *view, const ScoreEvent *event, double cycle) {
	return (ScoreControl){.time = round((event->start + cycle * event->period) * view->sample_rate) / view->sample_rate,
	    .cycle = cycle,
	    .order = event->order,
	    .value = event->value,
	    .stream = event->stream};
}

// First occurrence at or after time. Correct the estimate against sample-rounded
// timestamps, as the scheduler does. Negative pickups never fire at the beginning.
static double cycle_at(const ScoreView *view, const ScoreEvent *event, double time) {
	double limit = 0x1p53 - 1;
	double k = fmax(0, fmin(limit, floor((time - event->start) / event->period)));
	while (k > 0 && event->start + (k - 1) * event->period >= 0 && occurrence(view, event, k - 1).time >= time)
		--k;
	while (k < limit && (event->start + k * event->period < 0 || occurrence(view, event, k).time < time))
		++k;
	return k;
}

static void span(const ScoreView *view, const ScoreEvent *event, double from, double to, double *first, double *last) {
	if (event->period) {
		*first = cycle_at(view, event, from);
		*last = fmax(*first, cycle_at(view, event, to));
	} else {
		double time = occurrence(view, event, 0).time;
		*first = 0;
		*last = event->start >= 0 && time >= from && time < to;
	}
}

static int compare(const void *aa, const void *bb) {
	const ScoreControl *a = aa, *b = bb;
	if (a->time != b->time)
		return a->time < b->time ? -1 : 1;
	if (a->stream != b->stream)
		return a->stream < b->stream ? -1 : 1;
	if (a->cycle != b->cycle)
		return a->cycle < b->cycle ? -1 : 1;
	return (a->order > b->order) - (a->order < b->order);
}

static void include(ScoreAutomation *out, size_t bin, ScoreControl point) {
	out->low[bin] = fminf(out->low[bin], point.value);
	out->high[bin] = fmaxf(out->high[bin], point.value);
	if (compare(out->points + bin + 1, &point) < 0)
		out->points[bin + 1] = point;
}

int score_view_automation(
    const ScoreView *view, int node, int parameter, double from, double to, size_t bins, ScoreAutomation *out) {
	*out = (ScoreAutomation){0};
	if (node < 0 || node >= view->nnodes || parameter < 0 || parameter >= MAX_PARAMS || !view->sample_rate ||
	    !isfinite(from) || !isfinite(to) || from < 0 || from >= to || to * view->sample_rate >= 0x1p53 || !bins ||
	    bins > SCORE_AUTOMATION_POINTS)
		return -1;
	const ScoreNode *n = view->nodes + node;
	size_t begin = n->offsets[parameter], end = n->offsets[parameter + 1];
	if (begin == end)
		return -1;
	if (view->end > 0)
		to = fmin(to, view->end);
	if (from >= to)
		return 0;
	ScoreControl initial = {.time = -1, .value = n->defaults[parameter], .order = UINT64_MAX};
	double count = 0;
	for (size_t i = begin; i < end; ++i) {
		const ScoreEvent *e = n->events + n->controls[i];
		double first, last;
		span(view, e, from, to, &first, &last);
		count += last - first;
		// The last occurrence at or before the left edge determines the held value.
		double k = e->period ? cycle_at(view, e, nextafter(from, INFINITY)) - 1 : 0;
		if (k < 0 || e->start + k * e->period < 0)
			continue;
		ScoreControl prior = occurrence(view, e, k);
		if (prior.time <= from && compare(&initial, &prior) < 0)
			initial = prior;
	}
	initial.time = from;
	out->points[0] = initial;
	out->count = 1;
	if (count <= SCORE_AUTOMATION_POINTS) {
		for (size_t i = begin; i < end; ++i) {
			const ScoreEvent *e = n->events + n->controls[i];
			double first, last;
			span(view, e, from, to, &first, &last);
			for (double k = first; k < last; ++k)
				out->points[out->count++] = occurrence(view, e, k);
		}
		qsort(out->points + 1, out->count - 1, sizeof(ScoreControl), compare);
		// Multiple writes at one sample have only one audible value: the last wins.
		size_t used = 1;
		for (size_t i = 1; i < out->count; ++i) {
			if (out->points[i].time == out->points[used - 1].time)
				--used;
			out->points[used++] = out->points[i];
		}
		out->count = used;
		return 0;
	}
	out->dense = 1;
	out->count = bins + 1;
	for (size_t b = 0; b < bins; ++b) {
		out->low[b] = INFINITY;
		out->high[b] = -INFINITY;
		out->points[b + 1].time = -1;
	}
	for (size_t i = begin; i < end; ++i) {
		const ScoreEvent *e = n->events + n->controls[i];
		if (!e->period) {
			ScoreControl point = occurrence(view, e, 0);
			if (e->start >= 0 && point.time >= from && point.time < to) {
				size_t b = fmin(bins - 1, floor((point.time - from) / (to - from) * bins));
				include(out, b, point);
			}
			continue;
		}
		// A template contributes at most one value and its last timestamp to each
		// bin, regardless of how many millions of cycles the viewport spans.
		for (size_t b = 0; b < bins; ++b) {
			double first, last;
			span(view, e, from + (to - from) * b / bins, from + (to - from) * (b + 1) / bins, &first, &last);
			if (last > first)
				include(out, b, occurrence(view, e, last - 1));
		}
	}
	for (size_t b = 0; b < bins; ++b) {
		float value = out->points[b].value;
		out->low[b] = fminf(out->low[b], value);
		out->high[b] = fmaxf(out->high[b], value);
		if (out->points[b + 1].time < 0)
			out->points[b + 1] = out->points[b];
		out->points[b + 1].time = from + (to - from) * (b + 1) / bins;
	}
	return 0;
}
