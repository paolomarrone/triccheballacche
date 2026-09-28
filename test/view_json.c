#include "score_janet.h"
#include "score_view_json.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// JSON oracle consumed by view-json.mjs. Both adapters execute this same protocol implementation.
int main(int argc, char **argv) {
	assert(argc == 2);
	Score session = {.sample_rate = atoi(argv[1])};
	ScoreView view;
	Output output;
	assert(!prepare_score(&session, &output, "test/view-score.janet", NULL, NULL, &view));
	score_free(&session);
	const char *operations[] = {"score", "range", "range", "status", "origin", "range", "range", "range", "origin",
	    "unknown", "automation", "automation", "automation", "automation", "automation", "automation", "automation",
	    "automation"};
	const double args[][6] = {{0}, {7, 0, 7, 0, 8, 100}, {7, 0.2, 0.201, 0, 1, 50}, {0.32, 1}, {7, 0, 1},
	    {6, 0, 1, 0, 1, 100}, {7, 0, 1, 0, 1000, 100}, {7, NAN, 1, 0, 1, 100}, {7, 0, 1e30}, {0}, {7, 0, 1, 0, .4, 32},
	    {7, 0, 1, .25, .4, 32}, {6, 0, 1, 0, .4, 32}, {7, 0, 0, 0, .4, 32}, {7, 0, 1, NAN, .4, 32},
	    {7, 0, 1, 0, .4, 513}, {7, 0, 1, 8, 9, 32}, {7, 2, 1, 0, 7, 32}};
	for (size_t i = 0; i < sizeof(args) / sizeof(*args); ++i) {
		char *json = score_view_json(
		    &view, 7, operations[i], args[i][0], args[i][1], args[i][2], args[i][3], args[i][4], args[i][5]);
		assert(json);
		puts(json);
		free(json);
	}
	score_view_free(&view);
}
