# Optional native sampled piano. Run python plugins/piano/fetch.py first.
TSF ?= ../../.deps/tinysoundfont
SOUNDFONT ?= ../../.deps/florestan-piano/000_Florestan_Piano.sf2
CPPFLAGS += -isystem $(abspath $(TSF))
ifeq ($(PERONE_PLATFORM),wasm32)
$(error This optional piano uses native bundle file access)
endif
include ../plugin.mk

all: build/plugin.perone/piano.sf2

build/plugin.perone/piano.sf2: $(SOUNDFONT)
	mkdir -p build/plugin.perone
	cp "$<" "$@"

.PHONY: test
test: all
	$(CC) $(CPPFLAGS) -std=c99 -O2 -Wall -Wextra -Ibuild/gen/src test.c -lm -o build/test-piano
	./build/test-piano
