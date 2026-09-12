#ifndef SCORE_VIEW_JSON_H
#define SCORE_VIEW_JSON_H
#include "score_view.h"

// Shared editor protocol, independent of its transport. Caller frees the returned JSON; NULL means OOM.
// score: no arguments; status: time, playing; note: revision, node, order;
// range: revision, from, to, first lane, lane count, bin count. Unused arguments are zero.
char *score_view_json(const ScoreView *view, unsigned revision, const char *op, double a, double b, double c, double d,
    double e, double f);
#endif
