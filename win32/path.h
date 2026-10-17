#ifndef WIN32_PATH_H
#define WIN32_PATH_H
#include <wchar.h>
wchar_t *win32_wide(const char *text);
char *win32_utf8(const wchar_t *text);
#endif
