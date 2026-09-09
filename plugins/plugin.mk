# Generate/build a standalone Perone plugin; the host is not involved.
TIBIA ?= ../../../tibia
PLUGIN_DIR ?= .
PRODUCT ?= $(PLUGIN_DIR)/product.json
PLUGIN ?= $(PLUGIN_DIR)/plugin.h
TEMPLATES = $(wildcard $(TIBIA)/templates/perone/* $(TIBIA)/templates/perone/src/* $(TIBIA)/templates/perone-make/*) $(TIBIA)/templates/api/src/plugin_api.h
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
	$(MAKE) -C build/gen OUTPUT=../plugin.perone API_DIR=src PLUGIN_DIR=$(abspath $(PLUGIN_DIR)) CPPFLAGS="$(CPPFLAGS)"

clean:
	rm -rf build
