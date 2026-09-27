# Generate/build a standalone Perone plugin; the host is not involved.
TIBIA ?= ../../../tibia
CFLAGS ?= -O2 -Wall -Wextra
NODE ?= node
PLUGIN_DIR ?= .
PRODUCT ?= $(PLUGIN_DIR)/product.json
PLUGIN ?= $(PLUGIN_DIR)/plugin.h
TEMPLATES = $(wildcard $(TIBIA)/templates/api/* $(TIBIA)/templates/api/src/* \
    $(TIBIA)/templates/perone/* $(TIBIA)/templates/perone/src/* \
    $(TIBIA)/templates/perone-make/* $(TIBIA)/templates/web/src/*)
include $(dir $(lastword $(MAKEFILE_LIST)))wasm.mk
.DEFAULT_GOAL := all
.PHONY: all clean
all: build/plugin.perone

build/gen/Makefile: $(PRODUCT) $(PLUGIN) $(TEMPLATES) Makefile ../plugin.mk ../wasm.mk
	mkdir -p build/gen
	$(NODE) $(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/api build/gen/src
	$(NODE) $(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/perone build/gen
	$(NODE) $(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/perone-make build/gen

# Let the generated Makefile track all transitive DSP/header dependencies.
.PHONY: build/plugin.perone
build/plugin.perone: build/gen/Makefile
	$(MAKE) -C build/gen OUTPUT=../plugin.perone API_DIR=src PLUGIN_DIR=$(abspath $(PLUGIN_DIR)) \
	    CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" $(PERONE_BUILD)

clean:
	rm -rf build
