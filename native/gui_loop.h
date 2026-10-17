#ifndef GUI_LOOP_H
#define GUI_LOOP_H

// Initialize after opening the native WebView; --serve can use wait without init.
int gui_loop_init(void);
// Service window events while waiting for the next audio/plugin control tick.
void gui_loop_wait(void);
void gui_loop_free(void);

#endif
