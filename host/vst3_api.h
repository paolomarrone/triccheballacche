#ifndef HOST_VST3_API_H
#define HOST_VST3_API_H
#include <stdint.h>

// The generated C API defines a signed char16_t, conflicting with Android's
// standard uchar.h typedef. Keep that SDK type private without changing its ABI.
#define char16_t vst3_char16_t
#include <vst3_c_api.h>
#undef char16_t
#endif
