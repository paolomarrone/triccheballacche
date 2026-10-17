#include "test/platform.h"
// RLIMIT_FSIZE/fork fault injection is POSIX-only. The shared tests still cover
// render failures, existing destinations, atomic replacement and rename failures.
void test_export_write_failure(void (*failed_exports)(size_t, int), size_t block) {
	(void)failed_exports;
	(void)block;
}
