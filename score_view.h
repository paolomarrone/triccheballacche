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
	double start, end, period, max_end, min_note_end;
	uint64_t order;
	size_t first_origin, norigins;
	ScoreSummary summary;
	int pitch, velocity; // pitch -1: parameter; -2: note-off, removed when indexing.
	int parameter, stream;
	float value;
} ScoreEvent;

typedef struct {
	int inputs[MAX_NODES], ninputs;
	unsigned upstream, downstream;
	char *label;
	char *name, *bundle, *product; // Product JSON is encoded by Janet before teardown.
	float minimum[MAX_PARAMS], maximum[MAX_PARAMS];
	float defaults[MAX_PARAMS];
	uint64_t integers;
	ScoreEvent *events;
	size_t *by_order, count, raw_count;
	size_t *controls, offsets[MAX_PARAMS + 1]; // Event indices grouped by parameter, in time order.
} ScoreNode;

// An owned, immutable visual snapshot. Rendering never reads the live DSP or mixer state.
typedef struct {
	ScoreNode nodes[MAX_NODES];
	Track tracks[MAX_TRACKS + 1];
	int nnodes, ntracks, output;
	double end, active_from; // Source tracking starts at this revision's activation, not at hypothetical earlier notes.
	int repeating;
	unsigned sample_rate;
	ScoreOrigin *origins;
	size_t norigins, *references, nreferences;
} ScoreView;

// Copy actual scheduled events. Optional origins attach by node and original event order before indexing.
int score_view_init(ScoreView *view, const Session *session);
int score_view_index(ScoreView *view);
void score_view_free(ScoreView *view);
void score_view_remap(ScoreView *view, const int *mapping);
void score_view_activate(ScoreView *view, const Session *session);
// Overlapping events in [from, to). Sources use an 80 ms pulse; notes retain their exact duration.
// Returning zero from visit stops the query. Subtrees outside the interval are skipped.
void score_view_visit(const ScoreView *view, int node, double from, double to, int notes_only,
    int (*visit)(const ScoreEvent *, void *), void *context);
// Exact overlap counts and pitch bounds; fully covered subtrees are aggregated at once.
ScoreSummary score_view_summary(const ScoreView *view, int node, double from, double to);
const ScoreEvent *score_view_find(const ScoreView *view, int node, uint64_t order);

enum { SCORE_AUTOMATION_POINTS = 512 };
typedef struct {
	double time, cycle;
	uint64_t order;
	float value;
	int stream;
} ScoreControl;
typedef struct {
	ScoreControl points[SCORE_AUTOMATION_POINTS + 1];
	float low[SCORE_AUTOMATION_POINTS], high[SCORE_AUTOMATION_POINTS];
	size_t count;
	int dense;
} ScoreAutomation;
// Exact held values and changes, or bounded min/max bins when zoomed out.
// Includes the value at from; periodic templates are queried without expanding past cycles.
int score_view_automation(
    const ScoreView *view, int node, int parameter, double from, double to, size_t bins, ScoreAutomation *out);
#endif
