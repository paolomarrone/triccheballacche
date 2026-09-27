# Embedded sampled piano. Run python3 plugins/piano/fetch.py once for the bank.
PYTHON ?= python3
SOUNDFONT ?= ../../.deps/florestan-piano/000_Florestan_Piano.sf2
override CPPFLAGS += -I$(abspath build)
# The 7.5 MiB PCM bank is part of the standalone Wasm module.
WASM_INITIAL_MEMORY = 16777216
include ../plugin.mk

build/piano_data.h: $(SOUNDFONT) convert.py
	$(PYTHON) convert.py "$<" "$@"

build/plugin.perone: build/piano_data.h

.PHONY: test test-web
test: all
	$(PYTHON) test_convert.py
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -UNDEBUG -Ibuild/gen/src test.c -lm -o build/test-piano
	./build/test-piano

# Build the Wasm bundle with PERONE_PLATFORM=wasm32, then compare against native C.
test-web: test
	./build/test-piano --write-pcm build/reference.f32
	node test_web.mjs
