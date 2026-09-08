CC = cc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS = -ldl -lm -pthread
MINIAUDIO ?= $(firstword $(wildcard ../miniaudio.h) .deps/miniaudio.h)
JANET ?= .deps/janet
JANET_INCLUDES = -I$(JANET)/src/include -I$(JANET)/src/conf
JANET_LIBS = $(JANET)/build/libjanet.a
ifeq ($(shell uname -o 2>/dev/null),Android)
JANET_LIBS += -landroid-spawn
endif
TEST_PLUGINS = $(addsuffix /build/plugin.so,$(addprefix plugins/,synth_mono fx_svf tibia_test shape echo drums))
CORE = engine.c loader.c
HEADERS = engine.h loader.h module.h tibia/tibia.h

.PHONY: all test test-prog check-plugins run keys prog clean
all: build/host build/daw

build:
	mkdir -p $@

.deps/miniaudio.h:
	mkdir -p .deps
	curl -fL --retry 2 https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h -o $@.tmp
	mv $@.tmp $@

$(JANET)/Makefile:
	git clone --depth 1 --branch v1.41.2 https://github.com/janet-lang/janet.git $(JANET)

$(JANET)/build/libjanet.a: $(JANET)/Makefile
	$(MAKE) -C $(JANET) CC="$(CC)" CFLAGS="$(CFLAGS)" build/libjanet.a

build/audio.o: audio.c audio.h $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) -c audio.c -o $@

build/host: main.c $(CORE) $(HEADERS) audio.h build/audio.o | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) main.c $(CORE) build/audio.o $(LDFLAGS) $(LDLIBS) -o $@

build/test: loader_test.c $(CORE) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG loader_test.c $(CORE) $(LDFLAGS) $(LDLIBS) -o $@

build/termux_synth: examples/termux_synth/src/termux_synth.c $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) $< $(LDFLAGS) $(LDLIBS) -o $@

build/termux_test: examples/termux_synth/src/test.c examples/termux_synth/src/termux_synth.c $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I$(dir $(MINIAUDIO)) $< $(LDFLAGS) $(LDLIBS) -o $@

check-plugins:
	@for plugin in $(TEST_PLUGINS); do \
		test -f "$$plugin" || { echo "Missing $$plugin; build plugins separately: make -C plugins" >&2; exit 1; }; \
	done

test: check-plugins build/test build/termux_test build/daw_test
	./build/test
	./build/termux_test
	./build/daw_test

run: build/host
	./build/host plugins/synth_mono/build/plugin.so

keys: build/termux_synth
	./build/termux_synth --keys

build/daw: daw.c daw.h session.c session.h $(CORE) $(HEADERS) audio.h build/audio.o $(JANET)/build/libjanet.a | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) $(JANET_INCLUDES) daw.c session.c $(CORE) build/audio.o $(LDFLAGS) $(JANET_LIBS) $(LDLIBS) -o $@

build/daw_test: daw_test.c daw.c daw.h session.c session.h $(CORE) $(HEADERS) audio.h build/audio.o $(JANET)/build/libjanet.a | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDAW_TEST -I$(dir $(MINIAUDIO)) $(JANET_INCLUDES) daw_test.c daw.c session.c $(CORE) build/audio.o $(LDFLAGS) $(JANET_LIBS) $(LDLIBS) -o $@

prog: check-plugins build/daw
	./build/daw examples/prog/polpo.janet build/il_polpo_a_sette_gomiti.wav

test-prog: prog
	./build/daw examples/prog/polpo.janet build/polpo-repeat.wav
	cmp build/polpo-repeat.wav build/il_polpo_a_sette_gomiti.wav

clean:
	rm -f build/audio.o build/host build/test build/termux_synth build/termux_test build/daw build/daw_test
