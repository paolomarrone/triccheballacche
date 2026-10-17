#include "test/clap/plugin.h"
static LRESULT CALLBACK gui_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
	Plugin *p = (Plugin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (message == WM_NCCREATE)
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lp)->lpCreateParams);
	if (message == WM_LBUTTONDOWN && p) {
		p->values[0] = .75;
		p->edited = 1;
		p->host_params->request_flush(p->host);
	}
	return DefWindowProcW(hwnd, message, wp, lp);
}
bool gui_supported(const clap_plugin_t *plugin, const char *api, bool floating) {
	main_thread(self(plugin));
	return !floating && api && !strcmp(api, CLAP_WINDOW_API_WIN32);
}
bool gui_create(const clap_plugin_t *plugin, const char *api, bool floating) {
	Plugin *p = self(plugin);
	if (!gui_supported(plugin, api, floating) || p->display)
		return false;
	WNDCLASSW cls = {.lpfnWndProc = gui_proc, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"ClapTest"};
	if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;
	p->window = CreateWindowExW(
	    0, cls.lpszClassName, L"CLAP test", WS_CHILD, 0, 0, 240, 90, GetDesktopWindow(), NULL, cls.hInstance, p);
	if (!p->window)
		return false;
	p->display = p->window;
	assert(p->host_timer->register_timer(p->host, 10, &p->timer));
	return true;
}
void gui_destroy(const clap_plugin_t *plugin) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(p->host_timer->unregister_timer(p->host, p->timer));
	DestroyWindow(p->window);
	UnregisterClassW(L"ClapTest", GetModuleHandleW(NULL));
	p->display = NULL;
}
bool gui_size(const clap_plugin_t *plugin, uint32_t *w, uint32_t *h) {
	main_thread(self(plugin));
	*w = 240;
	*h = 90;
	return true;
}
bool gui_resizable(const clap_plugin_t *plugin) {
	main_thread(self(plugin));
	return false;
}
bool gui_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
	return SetParent(self(plugin)->window, window->win32) != NULL;
}
bool gui_show(const clap_plugin_t *plugin) {
	ShowWindow(self(plugin)->window, SW_SHOW);
	return true;
}
bool gui_hide(const clap_plugin_t *plugin) {
	ShowWindow(self(plugin)->window, SW_HIDE);
	return true;
}
void timer(const clap_plugin_t *plugin, clap_id id) {
	Plugin *p = self(plugin);
	main_thread(p);
	assert(id == p->timer);
}
