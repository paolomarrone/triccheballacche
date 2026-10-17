#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "host/window.h"
#include "path.h"
#include <clap/clap.h>
#include <stdlib.h>

typedef struct {
	HWND parent;
	int closed;
} NativeWindow;
static NativeWindow *native(PluginWindow *w) {
	return w->platform;
}

void *window_parent(PluginWindow *w) {
	return native(w)->parent;
}
void window_clap_parent(PluginWindow *w, struct clap_window *parent) {
	*parent = (clap_window_t){.api = CLAP_WINDOW_API_WIN32, .win32 = native(w)->parent};
}
int window_child_size(PluginWindow *w, void *child, unsigned *width, unsigned *height) {
	(void)w;
	RECT rect;
	if (!GetWindowRect(child, &rect))
		return -1;
	*width = rect.right - rect.left;
	*height = rect.bottom - rect.top;
	return 0;
}

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
	PluginWindow *w = (PluginWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (message == WM_NCCREATE) {
		w = ((CREATESTRUCTW *)lp)->lpCreateParams;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
	}
	if (message == WM_CLOSE && w) {
		native(w)->closed = 1;
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	}
	return DefWindowProcW(hwnd, message, wp, lp);
}
int window_size(PluginWindow *w, unsigned width, unsigned height) {
	if (!width || !height || width > 16384 || height > 16384)
		return -1;
	RECT rect = {0, 0, width, height};
	AdjustWindowRect(&rect, GetWindowLongW(native(w)->parent, GWL_STYLE), FALSE);
	if (!SetWindowPos(native(w)->parent, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
	        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
		return -1;
	w->width = width;
	w->height = height;
	return 0;
}
int window_open(PluginWindow *w, const char *title, unsigned width, unsigned height, int fixed) {
	w->platform = calloc(1, sizeof(NativeWindow));
	if (!w->platform)
		return -1;
	WNDCLASSW cls = {.lpfnWndProc = procedure,
	    .hInstance = GetModuleHandleW(NULL),
	    .hCursor = LoadCursor(NULL, IDC_ARROW),
	    .lpszClassName = L"TriccheballacchePlugin"};
	if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return -1;
	wchar_t *name = win32_wide(title);
	if (!name)
		return -1;
	w->fixed = fixed;
	DWORD style = WS_OVERLAPPEDWINDOW & ~(fixed ? WS_THICKFRAME | WS_MAXIMIZEBOX : 0);
	native(w)->parent = CreateWindowExW(
	    0, cls.lpszClassName, name, style, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, cls.hInstance, w);
	free(name);
	return native(w)->parent ? window_size(w, width, height) : -1;
}
void window_close(PluginWindow *w) {
	if (!w->platform)
		return;
	if (native(w)->parent)
		DestroyWindow(native(w)->parent);
	free(w->platform);
	*w = (PluginWindow){0};
}
void window_show(PluginWindow *w, int visible) {
	if (visible)
		native(w)->closed = 0;
	ShowWindow(native(w)->parent, visible ? SW_SHOW : SW_HIDE);
}
int window_poll(PluginWindow *w, int (*resize)(void *, unsigned *, unsigned *), void *context) {
	if (native(w)->closed)
		return 1;
	RECT rect;
	if (!GetClientRect(native(w)->parent, &rect))
		return -1;
	unsigned width = rect.right, height = rect.bottom;
	if (width && height && (width != w->width || height != w->height)) {
		if (w->fixed || !resize || resize(context, &width, &height))
			return window_size(w, w->width, w->height);
		return window_size(w, width, height);
	}
	return 0;
}
