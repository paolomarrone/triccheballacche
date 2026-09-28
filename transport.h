#ifndef TRANSPORT_H
#define TRANSPORT_H
#include "session.h"
#include "score_view.h"

// Control-thread state shared by both editors. Owns projections, borrows the
// attached session. The platform owns the device and confirms audio transitions.
typedef struct {
	Session *session;
	ScoreView view, queued;
	unsigned revision, control_revision;
	int playing;
	double time;
} Transport;

// Commit a successfully started session and move its projection. Revisions remain
// monotonic across runs; control_revision changes only when DSPs are replaced.
void transport_attach(Transport *t, Session *session, ScoreView *view);
void transport_started(Transport *t);
// Audio must be quiescent before acknowledging Stop or Seek.
void transport_stopped(Transport *t);
void transport_seeked(Transport *t);
// Publish a live revision using the device's sample clock. Moves both arguments
// on success; failure leaves them owned by the caller and current audio intact.
int transport_update(Transport *t, Score *score, ScoreView *view, uint64_t position);
// Adopt a revision observed at an audio boundary; returns whether the view changed.
int transport_collect(Transport *t);
int transport_live(const Transport *t);
// Detach before destroying the borrowed session. Retain the last visible score.
void transport_detach(Transport *t);
void transport_free(Transport *t);
// Common reply state and optional score/range/origin/automation/status projection.
// position is the device clock in seconds; a stopped transport uses its saved time.
char *transport_json(const Transport *t, const char *op, double position, const double args[6]);
#endif
