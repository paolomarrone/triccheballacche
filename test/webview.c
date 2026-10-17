#include "native/gui_loop.h"
#include "webui.h"
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

static atomic_int result = -1;

static void report(webui_event_t *event) {
	printf("%s\n", webui_get_string_at(event, 1));
	webui_return_string(event, "{}");
	atomic_store(&result, webui_get_bool_at(event, 0) ? 0 : 1);
}

int main(void) {
	size_t window = webui_new_window();
	webui_set_config(show_wait_connection, false);
	webui_set_timeout(0);
	webui_set_root_folder(window, ".");
	webui_set_size(window, 1000, 760);
	webui_bind(window, "report", report);
	int opened = webui_show_wv(window, "test/webview.html");
	if (opened && !gui_loop_init()) {
		time_t deadline = time(NULL) + 30;
		while (atomic_load(&result) < 0 && time(NULL) < deadline && webui_wait_async())
			gui_loop_wait();
	}
	gui_loop_free();
	webui_exit();
	webui_clean();
	if (atomic_load(&result) < 0)
		fputs("WebView test did not complete; a native display and WebView runtime are required.\n", stderr);
	return atomic_load(&result) != 0;
}
