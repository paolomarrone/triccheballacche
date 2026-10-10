// Shared by discovery and UI creation, without opening a display.
#include "host/clap.h"
#include "host/lv2.h"
#include "host/vst3.h"

const char *const vst_window_type = "X11EmbedWindowID";
const char *const clap_window_api = CLAP_WINDOW_API_X11;
const char *const lv2_ui_type = LV2_UI__X11UI;
