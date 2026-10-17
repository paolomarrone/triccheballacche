#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H
// Selected by the build; portability helpers belong to tests, not production code.
#include <test_platform.h>
void test_ui_wait(void);
void test_clap_click(void);
void test_export_write_failure(void (*failed_exports)(size_t, int), size_t block);
#endif
