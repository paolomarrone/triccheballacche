#include "script.h"

// A C call retains the Janet caller's frame, including a tail call.
// lib/trace.janet installs this observer only during traced score preparation.
static JanetCFunction push;

static Janet traced_push(int32_t argc, Janet *argv) {
	Janet result = push(argc, argv);
	Janet observer = janet_dyn("trace/push");
	if (argc > 1 && janet_checktype(observer, JANET_FUNCTION)) {
		Janet args[] = {result, janet_wrap_integer(janet_unwrap_array(result)->count - argc + 1),
		    janet_wrap_fiber(janet_current_fiber())};
		janet_call(janet_unwrap_function(observer), 3, args);
	}
	return result;
}

void script_trace(JanetTable *env) {
	Janet original;
	janet_resolve(env, janet_csymbol("array/push"), &original);
	push = janet_unwrap_cfunction(original);
	const JanetReg api[] = {
	    {"array-push", traced_push, "Internal source-tracing bridge; enabled by trace/install."}, {NULL, NULL, NULL}};
	janet_cfuns_prefix(env, "trace", api);
}
