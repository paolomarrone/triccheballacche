# Generate/build a standalone Tibia shared plugin; the host is not involved.
TIBIA ?= ../../../tibia
PLUGIN_DIR ?= .
PRODUCT ?= $(PLUGIN_DIR)/product.json
PLUGIN ?= $(PLUGIN_DIR)/plugin.h
TEMPLATES = $(wildcard $(TIBIA)/templates/shared/* $(TIBIA)/templates/shared/src/* $(TIBIA)/templates/shared-make/*) $(TIBIA)/templates/api/src/plugin_api.h
.DEFAULT_GOAL := all
.PHONY: all clean
all: build/plugin.so

build/gen/Makefile: $(PRODUCT) $(PLUGIN) $(TEMPLATES) Makefile ../plugin.mk
	mkdir -p build/gen
	$(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/api build/gen/src
	$(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/shared build/gen
	$(TIBIA)/tibia $(PRODUCT) $(TIBIA)/templates/shared-make build/gen

# Let the generated Makefile track all transitive DSP/header dependencies.
.PHONY: build/plugin.so
build/plugin.so: build/gen/Makefile
	$(MAKE) -C build/gen OUTPUT=../plugin.so API_DIR=src PLUGIN_DIR=$(abspath $(PLUGIN_DIR)) CPPFLAGS="$(CPPFLAGS)"

clean:
	rm -rf build
