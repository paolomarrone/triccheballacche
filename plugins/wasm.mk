# Shared target selection for the host, local plugins and canonical library.
PERONE_PLATFORM ?= $(shell uname -m)-$(if $(filter MINGW% MSYS%,$(shell uname -s)),windows,$(shell uname -s | tr A-Z a-z))
ifneq ($(filter MINGW% MSYS%,$(shell uname -s)),)
# MSYS2 Binaryen 133 hangs with its worker pool; make -j still parallelizes targets.
export BINARYEN_CORES ?= 1
endif
PERONE_BUILD = CC="$(CC)"
ifeq ($(PERONE_PLATFORM),wasm32)
EMCC ?= emcc
EMXX ?= em++
WASM_INITIAL_MEMORY ?= 1048576
PERONE_BUILD = CC="$(EMCC)" CXX="$(EMXX)" WASM_OBJS= TARGET_FLAGS="-DWASM" \
	TARGET_LDFLAGS="--no-entry -sSTANDALONE_WASM -sPURE_WASI -sMALLOC=emmalloc -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=$(WASM_INITIAL_MEMORY) \
	-Wl,--export=__wasm_call_ctors,--export=perone_get_api,--export=malloc,--export=free,--export=calloc,--export=realloc,--export-table,--growable-table"
else ifneq ($(filter %-windows,$(PERONE_PLATFORM)),)
# The pinned upstream Make template defaults to .so; keep its compilation rules.
PERONE_BUILD += 'BINARY=$$(BUILD_BIN_DIR)/$$(BUNDLE_NAME).dll' TARGET_FLAGS= TARGET_LDFLAGS="-shared -static"
endif
