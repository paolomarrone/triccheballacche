# Included by plugins/library.mk; upstream code is fetched once and built separately.
MDA_REV ?= 66d0536c142af567bb2fe4f547aa61379faaca00
MDA_URL ?= https://github.com/pluginguru/mda-effects.git
MDA_PERONE ?= build/library/mda
MDA_SOURCE := $(LIBRARY_DEPS)/mda-$(MDA_REV)
MDA_GEN := build/generated/mda/$(MDA_REV)-$(TIBIA_REV)
MDA_PORT := $(MDA_GEN)/source
MDA_NAMES := $(basename $(notdir $(filter-out %GUI.cpp,$(wildcard $(MDA_SOURCE)/mda-vst/src/*.cpp))))
MDA_FLAGS := -std=c++17 -O2 -fwrapv -Wno-multichar -Wno-write-strings
MDA_INCLUDES := -Iplugins/mda -I$(MDA_PORT)
MDA_JOBS := $(addprefix library-mda-,$(MDA_NAMES))
HOST_CXX ?= c++

library-deps: $(MDA_SOURCE)/.ready
library-build: library-mda

$(MDA_SOURCE)/.ready:
	mkdir -p "$(@D)"
	$(GIT) init -q "$(@D)"
	$(GIT) -C "$(@D)" fetch --depth 1 "$(MDA_URL)" "$(MDA_REV)"
	$(GIT) -C "$(@D)" checkout --detach "$(MDA_REV)"
	test -f "$(@D)/mda-vst/src/mdaPiano.cpp"
	touch "$@"

$(MDA_PORT)/.ready: $(MDA_SOURCE)/.ready plugins/mda/prepare.mjs patches/mda-perone.patch
	$(NODE) plugins/mda/prepare.mjs "$(MDA_SOURCE)" "$(MDA_PORT)"
	touch "$@"

.PRECIOUS: $(MDA_GEN)/%/Makefile
$(MDA_GEN)/%/Makefile: $(MDA_SOURCE)/mda-vst/src/%.cpp $(MDA_PORT)/.ready plugins/mda/mda.h plugins/mda/describe.cpp \
    plugins/mda/product.mjs plugins/mda/ui/controls.js plugins/mda/library.mk $(LIBRARY_DOT) $(LIBRARY_TEMPLATES)
	mkdir -p "$(@D)"
	printf '#include "%s.cpp"\n' '$*' > "$(@D)/dsp.h"
	$(HOST_CXX) $(MDA_FLAGS) $(MDA_INCLUDES) -I"$(@D)" plugins/mda/describe.cpp -o "$(@D)/describe"
	"$(@D)/describe" > "$(@D)/raw.json"
	$(NODE) plugins/mda/product.mjs "$*" "$(@D)/raw.json" "$(@D)/product.json"
	$(NODE) "$(TIBIA_SOURCE)/tibia" "$(@D)/product.json" "$(TIBIA_SOURCE)/templates/api" "$(@D)/src"
	$(NODE) "$(TIBIA_SOURCE)/tibia" "$(@D)/product.json" "$(TIBIA_SOURCE)/templates/perone" "$(@D)"
	$(NODE) "$(TIBIA_SOURCE)/tibia" "$(@D)/product.json" "$(TIBIA_SOURCE)/templates/perone-make" "$(@D)"

$(MDA_JOBS): WASM_INITIAL_MEMORY = 2097152

.PHONY: $(MDA_JOBS)
$(MDA_JOBS): library-mda-%: $(MDA_GEN)/%/Makefile
	$(MAKE) -f "$<" -I "$(<D)" OUTPUT="$(MDA_PERONE)/$*.perone" \
	    COMMON_DIR="$(<D)" DATA_DIR="$(<D)" API_DIR="$(<D)/src" PLUGIN_DIR="plugins/mda" \
	    CPPFLAGS='$(MDA_INCLUDES) -I$(<D) $(if $(filter mdaSpecMeter,$*),-DMDA_METER)' \
	    CXXFLAGS="$(MDA_FLAGS)" LDFLAGS="$(LDFLAGS)" PERONE_PLATFORM="$(PERONE_PLATFORM)" CXX="$(CXX)" $(PERONE_BUILD)
	mkdir -p "$(MDA_PERONE)/$*.perone/ui"
	cp plugins/mda/ui/* plugins/ui/panel.js editor/parameters.js "$(<D)/presets.json" "$(MDA_PERONE)/$*.perone/ui/"
	cat "$(MDA_SOURCE)/LICENSE" "$(MDA_SOURCE)/Reference/MIT License.txt" > "$(MDA_PERONE)/$*.perone/MDA-LICENSE"
	cp LICENSE "$(MDA_PERONE)/$*.perone/LICENSE"

.PHONY: library-mda
library-mda: $(MDA_JOBS)
	@test -n "$(MDA_NAMES)" || { echo "No MDA sources; run make library-deps first." >&2; exit 1; }
	@echo "Built $(words $(MDA_NAMES)) MDA Perone bundles ($(PERONE_PLATFORM)) in $(MDA_PERONE)"
