CC = cc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS = -ldl -lm -pthread
MINIAUDIO ?= $(firstword $(wildcard ../miniaudio.h) .deps/miniaudio.h)
BRICKWORKS ?= .deps/brickworks
BW_HEADER = $(BRICKWORKS)/include/bw_common.h
PLUGINS = examples/synth_mono/plugin.so examples/tibia_test/plugin.so

.PHONY: all test run keys clean
all: build/host $(PLUGINS) build/termux_synth

build:
	mkdir -p $@

.deps/miniaudio.h:
	mkdir -p .deps
	curl -fL --retry 2 https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h -o $@.tmp
	mv $@.tmp $@

$(BW_HEADER):
	git clone --depth 1 --branch v1.2.0 https://github.com/Orastron/brickworks.git $(BRICKWORKS)

$(BRICKWORKS)/.patched: $(BW_HEADER) Makefile
	# The sqrt approximation needs unsigned shifts (also checked with UBSan).
	sed -i 's/0x200000e0 <</0x200000e0u <</; s/0x100000f0 <</0x100000f0u <</' $(BRICKWORKS)/include/bw_math.h
	touch $@

build/host: main.c loader.c loader.h module.h tibia/tibia.h $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) main.c loader.c $(LDFLAGS) $(LDLIBS) -o $@

examples/synth_mono/plugin.so: examples/synth_mono/src/plugin.c tibia/tibia.h $(BRICKWORKS)/.patched
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -Itibia -I$(BRICKWORKS)/include -shared $< $(LDFLAGS) -lm -o $@

examples/tibia_test/plugin.so: examples/tibia_test/src/plugin.c tibia/tibia.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -Itibia -shared $< $(LDFLAGS) -lm -o $@

build/test: loader_test.c main.c loader.c loader.h module.h tibia/tibia.h $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I$(dir $(MINIAUDIO)) loader_test.c loader.c $(LDFLAGS) $(LDLIBS) -o $@

build/termux_synth: examples/termux_synth/src/termux_synth.c $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) $< $(LDFLAGS) $(LDLIBS) -o $@

build/termux_test: examples/termux_synth/src/test.c examples/termux_synth/src/termux_synth.c $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I$(dir $(MINIAUDIO)) $< $(LDFLAGS) $(LDLIBS) -o $@

test: build/test build/termux_test $(PLUGINS)
	./build/test
	./build/termux_test

run: all
	./build/host examples/synth_mono/plugin.so

keys: build/termux_synth
	./build/termux_synth --keys

clean:
	rm -f build/host build/test build/termux_synth build/termux_test $(PLUGINS)
