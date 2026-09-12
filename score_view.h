#ifndef SCORE_VIEW_H
#define SCORE_VIEW_H
#include "session.h"

typedef struct {
	char *file;
	int line, column;
} ScoreFrame;

typedef struct {
	ScoreFrame *frames;
	size_t count;
} ScoreOrigin;

typedef struct {
	size_t count;
	int low, high;
} ScoreSummary;

typedef struct {
	double start, end, max_end, min_note_end;
	size_t order, first_origin, norigins;
	ScoreSummary summary;
	int pitch, velocity; // pitch -1: parameter; -2: note-off, removed when indexing.
} ScoreEvent;

typedef struct {
	char *name;
	ScoreEvent *events;
	size_t *by_order, count, raw_count;
} ScoreNode;

// An owned, immutable visual snapshot. Rendering never reads the live DSP or mixer state.
typedef struct {
	ScoreNode nodes[MAX_NODES];
	Track tracks[MAX_TRACKS + 1];
	int nnodes, ntracks;
	double end;
	ScoreOrigin *origins;
	size_t norigins, *references, nreferences;
} ScoreView;

// Copy actual scheduled events. Optional origins attach by node and original event order before indexing.
int score_view_init(ScoreView *view, const Session *session);
int score_view_index(ScoreView *view);
void score_view_free(ScoreView *view);
// Overlapping events in [from, to). Sources use an 80 ms pulse; notes retain their exact duration.
// Returning zero from visit stops the query. Subtrees outside the interval are skipped.
void score_view_visit(const ScoreView *view, int node, double from, double to, int notes_only,
    int (*visit)(const ScoreEvent *, void *), void *context);
// Exact overlap counts and pitch bounds; fully covered subtrees are aggregated at once.
ScoreSummary score_view_summary(const ScoreView *view, int node, double from, double to);
const ScoreEvent *score_view_find(const ScoreView *view, int node, size_t order);
#endif
