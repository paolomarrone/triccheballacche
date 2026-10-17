#ifndef TEST_WIN32_PLATFORM_H
#define TEST_WIN32_PLATFORM_H
#include "host/platform.h"
#include <stdlib.h>
#include <unistd.h>
#define mkstemp host_mkstemp
static inline int setenv(const char *name, const char *value, int overwrite) {
	return !overwrite && getenv(name) ? 0 : _putenv_s(name, value);
}
static inline int unsetenv(const char *name) {
	return _putenv_s(name, "");
}
#endif
