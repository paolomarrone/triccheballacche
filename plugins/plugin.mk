# Generate/build a standalone Perone plugin; the host is not involved.
TIBIA ?= ../../../tibia
PLUGIN_DIR ?= .
PRODUCT ?= $(PLUGIN_DIR)/product.json
PLUGIN ?= $(PLUGIN_DIR)/plugin.h
TEMPLATES = $(wildcard $(TIBIA)/templates/perone/* $(TIBIA)/templates/perone/src/* $(TIBIA)/templates/perone-make/* $(TIBIA)/templates/web/src/*) $(TIBIA)/templates/api/src/plugin_api.h
ifeq ($(PERONE_PLATFORM),wasm32)
# These examples use libc/libm. Keep the Perone ABI, linking the C library into the standalone module.
EMCC ?= emcc
EMXX ?= em++
WASM_FLAGS = --no-entry -sSTANDALONE_WASM -sPURE_WASI -sMALLOC=emmalloc -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=1048576 \
	-Wl,--export=__wasm_call_ctors,--export=perone_get_api,--export=malloc,--export=free,--export=calloc,--export=realloc,--export-table,--growable-table
BUILD_OPTIONS = CC="$(EMCC)" CXX="$(EMXX)" WASM_OBJS= TARGET_FLAGS="-DWASM" TARGET_LDFLAGS="$(WASM_FLAGS)"
endif
.DEFAULT_GOAL := all
.PHONY: all clean
all: build/plugin.perone

build/gen/Makefile: $(PRODUCT) $(PLUGIN) $(TEMPLATES) Makefile ../plugin.mk
	mkdir -p build/gen
	$(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/api build/gen/src
	$(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/perone build/gen
	$(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/perone-make build/gen

# Let the generated Makefile track all transitive DSP/header dependencies.
.PHONY: build/plugin.perone
build/plugin.perone: build/gen/Makefile
	$(MAKE) -C build/gen OUTPUT=../plugin.perone API_DIR=src PLUGIN_DIR=$(abspath $(PLUGIN_DIR)) CPPFLAGS="$(CPPFLAGS)" $(BUILD_OPTIONS)

clean:
	rm -rf build
