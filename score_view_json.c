#include "score_view_json.h"
#include "json_write.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int integer(double value, double lo, double hi) {
	return isfinite(value) && value >= lo && value <= hi && value == floor(value);
}

static void metadata(Json *json, const ScoreView *view, unsigned revision) {
	json_print(json, ",\"score\":{\"revision\":%u,\"end\":%.17g,\"nodes\":[", revision, view->end);
	for (int i = 0; i < view->nnodes; ++i) {
		const ScoreNode *node = view->nodes + i;
		ScoreSummary summary = node->count ? node->events[node->count / 2].summary : (ScoreSummary){0};
		json_print(json, "%s{\"name\":", i ? "," : "");
		json_string(json, node->name);
		json_print(json, ",\"bundle\":");
		json_string(json, node->bundle);
		json_print(json, ",\"product\":%s,\"low\":%d,\"high\":%d}", node->product ? node->product : "null", summary.low,
		    summary.high);
	}
	json_print(json, "],\"tracks\":[");
	for (int i = 0; i < view->ntracks; ++i) {
		const Track *track = view->tracks + i;
		json_print(json, "%s[%d,%d", i ? "," : "", track->source, track->mixer);
		for (int j = 0; j < track->count; ++j)
			json_print(json, ",%d", track->effects[j]);
		json_print(json, "]");
	}
	json_print(json, "]}");
}

enum { MAX_FRAMES = 256, MAX_ACTIVE_EVENTS = 8192, MAX_LANES = 8, MAX_NOTES = 512, MAX_BINS = 512 };

typedef struct {
	const ScoreView *view;
	const ScoreFrame *frames[MAX_FRAMES];
	size_t count, visited;
	int truncated;
} Frames;

static int collect_frames(const ScoreEvent *event, void *context) {
	Frames *out = context;
	if (++out->visited > MAX_ACTIVE_EVENTS) {
		out->truncated = 1;
		return 0;
	}
	for (size_t i = 0; i < event->norigins; ++i) {
		const ScoreOrigin *origin = out->view->origins + out->view->references[event->first_origin + i];
		for (size_t j = 0; j < origin->count; ++j) {
			const ScoreFrame *frame = origin->frames + j;
			size_t k = 0;
			for (; k < out->count; ++k)
				if (out->frames[k]->line == frame->line && out->frames[k]->column == frame->column &&
				    !strcmp(out->frames[k]->file, frame->file))
					break;
			if (k < out->count)
				continue;
			if (out->count == MAX_FRAMES) {
				out->truncated = 1;
				return 0;
			}
			out->frames[out->count++] = frame;
		}
	}
	return 1;
}

static void write_frames(Json *json, const Frames *frames) {
	json_print(json, ",\"frames\":[");
	for (size_t i = 0; i < frames->count; ++i) {
		json_print(json, "%s[", i ? "," : "");
		json_string(json, frames->frames[i]->file);
		json_print(json, ",%d,%d]", frames->frames[i]->line, frames->frames[i]->column);
	}
	json_print(json, "],\"truncated\":%s", frames->truncated ? "true" : "false");
}

typedef struct {
	const ScoreEvent *events[MAX_NOTES];
	size_t count;
} Notes;

static int collect_notes(const ScoreEvent *event, void *context) {
	Notes *notes = context;
	notes->events[notes->count++] = event;
	return notes->count < MAX_NOTES;
}

static const char *range(
    Json *json, const ScoreView *view, double from, double to, double first, double count, double bins) {
	if (!isfinite(from) || !isfinite(to) || from < 0 || from >= to || !integer(first, 0, view->ntracks) ||
	    !integer(count, 1, MAX_LANES) || !integer(bins, 1, MAX_BINS))
		return "Intervallo della vista non valido";
	json_print(json, ",\"from\":%.17g,\"to\":%.17g,\"first\":%.0f,\"lanes\":[", from, to, first);
	for (int i = first; i < first + count && i < view->ntracks; ++i) {
		int node = view->tracks[i].source;
		ScoreSummary summary = score_view_summary(view, node, from, to);
		json_print(json, "%s{\"count\":%zu,", i != first ? "," : "", summary.count);
		if (summary.count <= MAX_NOTES) {
			Notes notes = {0};
			score_view_visit(view, node, from, to, 1, collect_notes, &notes);
			json_print(json, "\"notes\":[");
			for (size_t j = 0; j < notes.count; ++j) {
				const ScoreEvent *n = notes.events[j];
				json_print(
				    json, "%s[%zu,%.17g,%.17g,%d,%d]", j ? "," : "", n->order, n->start, n->end, n->pitch, n->velocity);
			}
			json_print(json, "]}");
		} else {
			json_print(json, "\"density\":[");
			for (int j = 0; j < bins; ++j) {
				double a = from + (to - from) * j / bins, b = from + (to - from) * (j + 1) / bins;
				ScoreSummary bin = score_view_summary(view, node, a, b);
				json_print(json, "%s[%zu,%d,%d]", j ? "," : "", bin.count, bin.low, bin.high);
			}
			json_print(json, "]}");
		}
	}
	json_print(json, "]");
	return NULL;
}

char *score_view_json(const ScoreView *view, unsigned revision, const char *op, double a, double b, double c, double d,
    double e, double f) {
	static const ScoreView empty;
	if (!view)
		view = &empty;
	Json json = {0};
	const char *error = NULL;
	json_print(&json, "{\"revision\":%u", revision);
	if ((!strcmp(op, "range") || !strcmp(op, "note")) && a != revision) {
		json_print(&json, ",\"stale\":true");
	} else if (!strcmp(op, "score")) {
		metadata(&json, view, revision);
	} else if (!strcmp(op, "range")) {
		error = range(&json, view, b, c, d, e, f);
	} else if (!strcmp(op, "note")) {
		const ScoreEvent *note = integer(b, 0, view->nnodes - 1) && integer(c, 0, view->nodes[(int)b].raw_count) &&
		        c < view->nodes[(int)b].raw_count
		    ? score_view_find(view, (int)b, (size_t)c)
		    : NULL;
		Frames frames = {.view = view};
		if (note && note->pitch >= 0)
			collect_frames(note, &frames);
		write_frames(&json, &frames);
	} else if (!strcmp(op, "status")) {
		Frames frames = {.view = view};
		for (int i = 0; b && !frames.truncated && i < view->nnodes; ++i)
			score_view_visit(view, i, a, nextafter(a, INFINITY), 0, collect_frames, &frames);
		write_frames(&json, &frames);
	} else {
		error = "Query sconosciuta";
	}
	json_print(&json, ",\"error\":");
	json_string(&json, error);
	json_print(&json, "}");
	if (json.failed) {
		free(json.data);
		return NULL;
	}
	return json.data;
}
