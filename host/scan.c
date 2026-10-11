#include "scan.h"

void scan_error(Json *j, const char *format, const char *path, const char *id, const char *name, const char *reason) {
	json_print(j, "{\"id\":");
	json_string(j, id);
	json_print(j, ",\"binary\":");
	json_string(j, path);
	json_print(j, ",\"error\":");
	json_string(j, reason);
	json_print(j, ",\"product\":{\"format\":");
	json_string(j, format);
	json_print(j, ",\"name\":");
	json_string(j, name ? name : id);
	json_print(j, "}}");
}
