#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "native/gui_loop.h"

int gui_loop_init(void) {
	return 0;
}
void gui_loop_wait(void) {
	MSG message;
	while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	MsgWaitForMultipleObjects(0, NULL, FALSE, 16, QS_ALLINPUT);
}
void gui_loop_free(void) {
}
