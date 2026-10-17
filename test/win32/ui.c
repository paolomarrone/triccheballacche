#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "test/platform.h"
#include "native/gui_loop.h"
#include <assert.h>
void test_ui_wait(void) {
	gui_loop_wait();
}
void test_clap_click(void) {
	HWND parent = FindWindowW(L"TriccheballacchePlugin", L"CLAP test synth");
	assert(parent);
	HWND child = GetWindow(parent, GW_CHILD);
	assert(child);
	SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 30));
}
