#ifndef ASSETS_H
#define ASSETS_H
#include "score_view.h"

// WebUI's HTTP threads borrow only owned paths, never the live Session or ScoreView.
void assets_set(const ScoreView *view, unsigned revision);
const void *assets_read(const char *url, int *length);
#endif
