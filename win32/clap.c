#include "host/clap.h"

// CLAP's POSIX descriptor extension is not offered on Windows. Timers and
// main-thread callbacks are serviced by the shared adapter.
const void *clap_platform_extension(const char *id) {
	(void)id;
	return NULL;
}
int clap_platform_open(Clap *c) {
	(void)c;
	return 0;
}
void clap_platform_close(Clap *c) {
	(void)c;
}
void clap_platform_poll(Clap *c) {
	(void)c;
}
