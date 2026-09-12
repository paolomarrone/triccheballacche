#include "daw.h"
#include "score_view_json.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// JSON oracle consumed by view-json.mjs. Both adapters execute this same protocol implementation.
int main(int argc, char **argv) {
	assert(argc == 2);
	Session session = {.sample_rate = atoi(argv[1])};
	ScoreView view;
	Output output;
	assert(!prepare_score(&session, &output, "test/view-score.janet", NULL, NULL, &view));
	session_free(&session);
	const char *operations[] = {
	    "score", "range", "range", "status", "note", "range", "range", "range", "note", "unknown"};
	const double args[][6] = {{0}, {7, 0, 7, 0, 8, 100}, {7, 0.2, 0.201, 0, 1, 50}, {0.32, 1}, {7, 0, 1},
	    {6, 0, 1, 0, 1, 100}, {7, 0, 1, 0, 1000, 100}, {7, NAN, 1, 0, 1, 100}, {7, 0, 1e30}, {0}};
	for (size_t i = 0; i < sizeof(args) / sizeof(*args); ++i) {
		char *json = score_view_json(
		    &view, 7, operations[i], args[i][0], args[i][1], args[i][2], args[i][3], args[i][4], args[i][5]);
		assert(json);
		puts(json);
		free(json);
	}
	score_view_free(&view);
}
