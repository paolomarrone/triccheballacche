/* Perone UI ABI v1. GPL-3.0-or-later. */
#ifndef PERONE_UI_H
#define PERONE_UI_H
#include <stddef.h>
#include <stdint.h>

#define PERONE_UI_ABI_VERSION 1
#define PERONE_UI_X11 1

typedef struct {
	void *handle;
	const char *(*get_bindir)(void *handle);
	const char *(*get_datadir)(void *handle);
	void (*set_parameter_begin)(void *handle, size_t index, float value);
	void (*set_parameter)(void *handle, size_t index, float value);
	void (*set_parameter_end)(void *handle, size_t index, float value);
	void (*msg_write)(void *handle, size_t size, const void *data);
} perone_ui_callbacks;

typedef struct {
	void (*get_default_size)(uint32_t *width, uint32_t *height);
	void *(*create)(uint32_t window_api, char has_parent, void *parent, const perone_ui_callbacks *callbacks);
	void (*free)(void *ui);
	void (*idle)(void *ui);
	void (*set_parameter)(void *ui, size_t index, float value);
	void (*msg_in)(void *ui, size_t size, const void *data);
	void *(*get_widget)(void *ui);
} perone_ui_api;

/* Separate, optional UI library; the DSP ABI is unchanged.
 * create returns NULL for unsupported window APIs or source creation failure.
 * X11 parent/widget handles are XIDs cast through uintptr_t to void *.
 * Call all UI functions on the UI thread; keep the library loaded until free.
 * free requires a successfully created UI.
 * The host routes parameter gestures and messages, and schedules idle calls.
 * Callbacks use product units and original JSON indices. format = "perone".
 * The host sends current parameter values after create and after state changes.
 * Optional set_parameter/msg_in entries are NULL when the capability is absent.
 * get_widget exposes the source UI's widget without exposing its private layout.
 */
#ifdef __cplusplus
extern "C" {
#endif
const perone_ui_api *perone_ui_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
#endif
