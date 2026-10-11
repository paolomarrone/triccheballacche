#ifndef HOST_VST3_API_H
#define HOST_VST3_API_H
#include <stdint.h>

// Keep the SDK's signed char16_t private so it can coexist with <uchar.h>.
#define char16_t vst3_char16_t
#include <vst3_c_api.h>
#undef char16_t
#endif
