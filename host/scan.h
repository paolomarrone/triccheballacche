#ifndef HOST_SCAN_H
#define HOST_SCAN_H
#include "json_write.h"

// A rejected catalog entry keeps its identity and reason, but has no DSP description.
void scan_error(Json *json, const char *format, const char *path, const char *id, const char *name, const char *reason);
#endif
