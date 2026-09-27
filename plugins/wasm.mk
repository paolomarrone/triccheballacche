# Standalone Perone DSPs with Emscripten's libc/libm, shared by both plugin builds.
PERONE_BUILD = CC="$(CC)"
ifeq ($(PERONE_PLATFORM),wasm32)
EMCC ?= emcc
EMXX ?= em++
WASM_INITIAL_MEMORY ?= 1048576
PERONE_BUILD = CC="$(EMCC)" CXX="$(EMXX)" WASM_OBJS= TARGET_FLAGS="-DWASM" \
	TARGET_LDFLAGS="--no-entry -sSTANDALONE_WASM -sPURE_WASI -sMALLOC=emmalloc -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=$(WASM_INITIAL_MEMORY) \
	-Wl,--export=__wasm_call_ctors,--export=perone_get_api,--export=malloc,--export=free,--export=calloc,--export=realloc,--export-table,--growable-table"
endif
