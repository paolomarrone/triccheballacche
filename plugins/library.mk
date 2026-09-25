# Canonical Brickworks C library. Run from the repository root.
# Tibia's Perone templates live on its perone branch, not main.
BRICKWORKS_REV ?= d3695e8a4837db2c22e1a75458f43c048a90912f
TIBIA_REV ?= 76760dd7b9cc3e595a3453673de60cfe6684a657
BRICKWORKS_URL ?= https://github.com/Orastron/brickworks.git
TIBIA_URL ?= https://github.com/paolomarrone/tibia.git
LIBRARY_DEPS ?= .deps/library
BRICKWORKS_PERONE ?= build/library/brickworks
GIT ?= git
NODE ?= node
NPM ?= npm
DOT_VERSION := 1.1.3
BW_SOURCE := $(abspath $(LIBRARY_DEPS)/brickworks-$(BRICKWORKS_REV))
TIBIA_SOURCE := $(abspath $(LIBRARY_DEPS)/tibia-$(TIBIA_REV))
LIBRARY_DOT := $(TIBIA_SOURCE)/node_modules/dot/.triccheballacche-$(DOT_VERSION)
LIBRARY_GEN := build/generated/library/$(BRICKWORKS_REV)-$(TIBIA_REV)
LIBRARY_DEST := $(abspath $(BRICKWORKS_PERONE))
PERONE_PLATFORM ?= $(shell uname -m)-$(shell uname -s | tr A-Z a-z)
CFLAGS ?= -O2 -Wall -Wextra

.PHONY: library library-deps library-build
library-deps: $(BW_SOURCE)/.ready $(LIBRARY_DOT)

# A second make discovers the examples only after a fresh download has finished.
# Recursive make also shares the parent's jobserver for make -j builds.
library: library-deps
	$(MAKE) -f plugins/library.mk library-build CC="$(CC)" CFLAGS="$(CFLAGS)" \
	    CPPFLAGS="$(CPPFLAGS)" LDFLAGS="$(LDFLAGS)" PERONE_PLATFORM="$(PERONE_PLATFORM)"

# These revision-specific directories are managed caches, never sibling checkouts.
# The marker is written last so an interrupted fetch can be retried.
$(BW_SOURCE)/.ready:
	mkdir -p "$(@D)"
	$(GIT) init -q "$(@D)"
	$(GIT) -C "$(@D)" fetch --depth 1 "$(BRICKWORKS_URL)" "$(BRICKWORKS_REV)"
	$(GIT) -C "$(@D)" checkout --detach "$(BRICKWORKS_REV)"
	test -f "$(@D)/examples/common/src/company.json"
	touch "$@"

$(TIBIA_SOURCE)/.ready:
	mkdir -p "$(@D)"
	$(GIT) init -q "$(@D)"
	$(GIT) -C "$(@D)" fetch --depth 1 "$(TIBIA_URL)" "$(TIBIA_REV)"
	$(GIT) -C "$(@D)" checkout --detach "$(TIBIA_REV)"
	test -f "$(@D)/templates/perone-make/Makefile"
	touch "$@"

$(LIBRARY_DOT): $(TIBIA_SOURCE)/.ready
	$(NPM) install --prefix "$(TIBIA_SOURCE)" --no-save --package-lock=false \
	    --ignore-scripts --no-audit --no-fund dot@$(DOT_VERSION)
	$(NODE) -e 'require(process.argv[1])' "$(TIBIA_SOURCE)/node_modules/dot"
	touch "$@"

# These patterns deliberately exclude the duplicate fxpp_* and synthpp_* examples.
BW_PRODUCTS := $(sort $(wildcard $(BW_SOURCE)/examples/fx_*/src/product.json $(BW_SOURCE)/examples/synth_*/src/product.json))
BW_EXAMPLES := $(notdir $(patsubst %/src/product.json,%,$(BW_PRODUCTS)))
BW_VERSION := $(shell sed -n 's/^VERSION=//p' "$(BW_SOURCE)/examples/tibia_gen.sh" 2>/dev/null)
LIBRARY_TEMPLATES := $(wildcard $(TIBIA_SOURCE)/templates/api/* $(TIBIA_SOURCE)/templates/api/src/* \
	$(TIBIA_SOURCE)/templates/perone/* $(TIBIA_SOURCE)/templates/perone/src/* \
	$(TIBIA_SOURCE)/templates/perone-make/* $(TIBIA_SOURCE)/templates/web/src/*)
LIBRARY_JOBS := $(addprefix library-build-,$(BW_EXAMPLES))

library-build: $(LIBRARY_JOBS)
	@test -n "$(BW_EXAMPLES)" || { echo "No Brickworks C examples found; run make library first." >&2; exit 1; }
	@echo "Built $(words $(BW_EXAMPLES)) Brickworks C Perone bundles ($(PERONE_PLATFORM)) in $(BRICKWORKS_PERONE)"

# Keep generated files after the pattern rules finish; builds stay incremental.
.PRECIOUS: $(LIBRARY_GEN)/%/Makefile
$(LIBRARY_GEN)/%/Makefile: $(BW_SOURCE)/examples/%/src/product.json $(BW_SOURCE)/examples/%/src/plugin.h \
	$(BW_SOURCE)/examples/common/src/company.json $(BW_SOURCE)/.ready \
	$(LIBRARY_DOT) $(LIBRARY_TEMPLATES) plugins/library.mk
	mkdir -p "$(@D)"
	$(NODE) "$(TIBIA_SOURCE)/tibia" "$(BW_SOURCE)/examples/common/src/company.json,$<" \
	    "$(TIBIA_SOURCE)/templates/api" "$(@D)/src" 'product.version="$(BW_VERSION)"' 'product.buildVersion="1"'
	$(NODE) "$(TIBIA_SOURCE)/tibia" "$(BW_SOURCE)/examples/common/src/company.json,$<" \
	    "$(TIBIA_SOURCE)/templates/perone" "$(@D)" 'product.version="$(BW_VERSION)"' 'product.buildVersion="1"'
	$(NODE) "$(TIBIA_SOURCE)/tibia" "$(BW_SOURCE)/examples/common/src/company.json,$<" \
	    "$(TIBIA_SOURCE)/templates/perone-make" "$(@D)" 'product.version="$(BW_VERSION)"' 'product.buildVersion="1"'

LIBRARY_COMPILER = CC="$(CC)"
ifeq ($(PERONE_PLATFORM),wasm32)
EMCC ?= emcc
LIBRARY_COMPILER = CC="$(EMCC)" WASM_OBJS= TARGET_FLAGS="-DWASM" \
	TARGET_LDFLAGS="--no-entry -sSTANDALONE_WASM -sPURE_WASI -sMALLOC=emmalloc -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=1048576 \
	-Wl,--export=__wasm_call_ctors,--export=perone_get_api,--export=malloc,--export=free,--export=calloc,--export=realloc,--export-table,--growable-table"
endif

.PHONY: $(LIBRARY_JOBS)
$(LIBRARY_JOBS): library-build-%: $(LIBRARY_GEN)/%/Makefile
	$(MAKE) -C "$(<D)" OUTPUT="$(LIBRARY_DEST)/$*/build/bw_example_$*.perone" \
	    API_DIR=src PLUGIN_DIR="$(BW_SOURCE)/examples/$*/src" \
	    CPPFLAGS="$(CPPFLAGS) -I$(BW_SOURCE)/include -I$(BW_SOURCE)/examples/common/src" \
	    CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" PERONE_PLATFORM="$(PERONE_PLATFORM)" $(LIBRARY_COMPILER)
