#include "gui_loop.h"
#include <time.h>

int gui_loop_init(void) {
	return 0;
}

void gui_loop_wait(void) {
	nanosleep(&(struct timespec){.tv_nsec = 16000000}, NULL);
}

void gui_loop_free(void) {
}
