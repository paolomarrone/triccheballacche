CC = cc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS = -ldl -lm -pthread
MINIAUDIO ?= $(firstword $(wildcard ../miniaudio.h) .deps/miniaudio.h)
BRICKWORKS ?= .deps/brickworks
JANET ?= .deps/janet
JANET_INCLUDES = -I$(JANET)/src/include -I$(JANET)/src/conf
JANET_LIBS = $(JANET)/build/libjanet.a
ifeq ($(shell uname -o 2>/dev/null),Android)
JANET_LIBS += -landroid-spawn
endif
BW_HEADER = $(BRICKWORKS)/include/bw_common.h
PLUGINS = examples/synth_mono/plugin.so examples/tibia_test/plugin.so

.PHONY: all test test-prog run keys prog clean
all: build/host $(PLUGINS) build/termux_synth build/daw

build:
	mkdir -p $@

.deps/miniaudio.h:
	mkdir -p .deps
	curl -fL --retry 2 https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h -o $@.tmp
	mv $@.tmp $@

$(BW_HEADER):
	git clone --depth 1 --branch v1.2.0 https://github.com/Orastron/brickworks.git $(BRICKWORKS)

$(JANET)/Makefile:
	git clone --depth 1 --branch v1.41.2 https://github.com/janet-lang/janet.git $(JANET)

$(JANET)/build/libjanet.a: $(JANET)/Makefile
	$(MAKE) -C $(JANET) CC="$(CC)" CFLAGS="$(CFLAGS)" build/libjanet.a

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

test: build/test build/termux_test build/daw_test $(PLUGINS)
	./build/test
	./build/termux_test
	./build/daw_test

run: all
	./build/host examples/synth_mono/plugin.so

keys: build/termux_synth
	./build/termux_synth --keys

build/daw: daw.c main.c loader.c loader.h module.h tibia/tibia.h $(MINIAUDIO) $(JANET)/build/libjanet.a | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) $(JANET_INCLUDES) $< loader.c $(LDFLAGS) $(JANET_LIBS) $(LDLIBS) -o $@

build/daw_test: daw_test.c daw.c main.c loader.c loader.h module.h tibia/tibia.h $(MINIAUDIO) $(JANET)/build/libjanet.a | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I$(dir $(MINIAUDIO)) $(JANET_INCLUDES) $< loader.c $(LDFLAGS) $(JANET_LIBS) $(LDLIBS) -o $@

prog: build/daw examples/synth_mono/plugin.so
	./build/daw examples/prog/polpo.janet build/il_polpo_a_sette_gomiti.wav

test-prog: prog
	cmp examples/prog/il_polpo_a_sette_gomiti.wav build/il_polpo_a_sette_gomiti.wav

clean:
	rm -f build/host build/test build/termux_synth build/termux_test build/daw build/daw_test $(PLUGINS)
