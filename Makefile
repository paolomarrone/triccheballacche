CC = cc
CFLAGS ?= -O2 -Wall -Wextra
LDLIBS = -ldl -lm -pthread
MINIAUDIO ?= $(firstword $(wildcard ../miniaudio.h) .deps/miniaudio.h)
JANET ?= .deps/janet
JANET_INCLUDES = -I$(JANET)/src/include -I$(JANET)/src/conf
JANET_LIBS = $(JANET)/build/libjanet.a
SPORK ?= .deps/spork
SPORK_REV = 0667f96b74de52747ffe5e19e185563ebf53816b
PERONE_PLATFORM ?= $(shell uname -m)-$(shell uname -s | tr A-Z a-z)
SCRIPT_FLAGS = $(JANET_INCLUDES) -DPERONE_PLATFORM='"$(PERONE_PLATFORM)"'
SCRIPT = script.c script.h build/perone.inc build/json.o
SCRIPT_LIBS = build/json.o $(JANET_LIBS)
ifeq ($(shell uname -o 2>/dev/null),Android)
JANET_LIBS += -landroid-spawn
endif
TEST_PLUGINS = $(addsuffix /build/plugin.perone,$(addprefix plugins/,synth_mono fx_svf tibia_test shape echo drums))
CORE = engine.c loader.c
HEADERS = engine.h loader.h module.h perone.h
FORMAT_SOURCES = $(filter-out perone.h,$(wildcard *.c *.h test/*.c test/perone/*.c plugins/*/plugin.h examples/termux_synth/src/*.c))

.PHONY: all test test-prog test-brickworks check-plugins run keys prog clean format format-check
all: build/host build/daw

format:
	clang-format -i $(FORMAT_SOURCES)

format-check:
	clang-format --dry-run --Werror $(FORMAT_SOURCES)

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

$(SPORK)/src/json.c:
	mkdir -p $(dir $@)
	curl -fL --retry 2 https://raw.githubusercontent.com/janet-lang/spork/$(SPORK_REV)/src/json.c -o $@.tmp
	mv $@.tmp $@

build/json.o: $(SPORK)/src/json.c $(JANET)/build/libjanet.a | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(JANET_INCLUDES) -DJANET_ENTRY_NAME=janet_json -c $< -o $@

build/%.inc: lib/%.janet | build
	sed -e 's/[\\"]/\\&/g' -e 's/^/"/' -e 's/$$/\\n"/' $< > $@

build/host: main.c $(CORE) $(HEADERS) $(SCRIPT) audio.h build/audio.o | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) $(SCRIPT_FLAGS) main.c $(CORE) script.c build/audio.o $(LDFLAGS) $(SCRIPT_LIBS) $(LDLIBS) -o $@

build/test: test/loader.c $(CORE) $(HEADERS) $(SCRIPT) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. $(SCRIPT_FLAGS) test/loader.c $(CORE) script.c $(LDFLAGS) $(SCRIPT_LIBS) $(LDLIBS) -o $@

build/termux_synth: examples/termux_synth/src/termux_synth.c $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) $< $(LDFLAGS) $(LDLIBS) -o $@

build/termux_test: test/termux.c examples/termux_synth/src/termux_synth.c $(MINIAUDIO) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -I$(dir $(MINIAUDIO)) $< $(LDFLAGS) $(LDLIBS) -o $@

check-plugins:
	@for plugin in $(TEST_PLUGINS); do \
		test -f "$$plugin/product.json" || { echo "Missing $$plugin; build plugins separately: make -C plugins" >&2; exit 1; }; \
	done

TEST_BUNDLE = build/fixture.perone
$(TEST_BUNDLE)/$(PERONE_PLATFORM)/fixture.so: test/perone/plugin.c perone.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -fPIC -fvisibility=hidden -shared $< -o $@

$(TEST_BUNDLE)/product.json: test/perone/product.json
	mkdir -p $(dir $@)
	cp $< $@

test: check-plugins build/test build/termux_test build/daw_test $(TEST_BUNDLE)/$(PERONE_PLATFORM)/fixture.so $(TEST_BUNDLE)/product.json
	./build/test
	./build/termux_test
	./build/daw_test

run: build/host
	./build/host plugins/synth_mono/build/plugin.perone

keys: build/termux_synth
	./build/termux_synth --keys

build/daw: daw.c daw.h session.c session.h $(CORE) $(HEADERS) audio.h build/audio.o $(SCRIPT) build/daw.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) $(SCRIPT_FLAGS) daw.c session.c $(CORE) script.c build/audio.o $(LDFLAGS) $(SCRIPT_LIBS) $(LDLIBS) -o $@

build/daw_test: test/daw.c daw.c daw.h session.c session.h $(CORE) $(HEADERS) audio.h build/audio.o $(SCRIPT) build/daw.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -DDAW_TEST -I$(dir $(MINIAUDIO)) $(SCRIPT_FLAGS) test/daw.c daw.c session.c $(CORE) script.c build/audio.o $(LDFLAGS) $(SCRIPT_LIBS) $(LDLIBS) -o $@

prog: check-plugins build/daw
	./build/daw examples/prog/polpo.janet build/il_polpo_a_sette_gomiti.wav

test-prog: prog
	./build/daw examples/prog/polpo.janet build/polpo-repeat.wav
	cmp build/polpo-repeat.wav build/il_polpo_a_sette_gomiti.wav

clean:
	rm -f build/audio.o build/json.o build/perone.inc build/daw.inc build/host build/test build/termux_synth build/termux_test build/daw build/daw_test
	rm -rf build/fixture.perone

# Read-only audit of the bundles built in Brickworks; no plugin compilation here.
BRICKWORKS_PERONE ?= ../brickworks/build/perone
BW_BUNDLES = $(wildcard $(BRICKWORKS_PERONE)/*/build/*.perone)
test-brickworks: build/test
	@test -n "$(BW_BUNDLES)" || { echo "No Perone bundles in $(BRICKWORKS_PERONE)"; exit 1; }
	./build/test $(BW_BUNDLES)
