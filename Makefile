CC = cc
CFLAGS ?= -O2 -Wall -Wextra
TARGET_OS ?= $(shell uname -s)
LDLIBS = -lm -pthread
ifneq ($(TARGET_OS),Darwin)
LDLIBS += -ldl
endif
MINIAUDIO ?= $(firstword $(wildcard ../miniaudio.h) .deps/miniaudio.h)
JANET ?= .deps/janet
JANET_INCLUDES = -I$(JANET)/src/include -I$(JANET)/src/conf
JANET_LIBS = $(JANET)/build/libjanet.a
SPORK ?= .deps/spork
SPORK_REV = 0667f96b74de52747ffe5e19e185563ebf53816b
PERONE_PLATFORM ?= $(shell uname -m)-$(shell echo $(TARGET_OS) | tr A-Z a-z)
PERONE_SUFFIX ?= .so
SCRIPT_FLAGS = $(JANET_INCLUDES) -DPERONE_SUFFIX='"$(PERONE_SUFFIX)"' -DPERONE_PLATFORM='"$(PERONE_PLATFORM)"'
SCRIPT = script.c script.h build/perone.inc build/json.o
SCRIPT_LIBS = build/json.o $(JANET_LIBS)
ifeq ($(shell uname -o 2>/dev/null),Android)
JANET_LIBS += -landroid-spawn
endif
TEST_PLUGINS = $(addsuffix /build/plugin.perone,$(addprefix plugins/,synth_mono fx_svf tibia_test shape echo drums))
CORE = engine.c loader.c
SCORE_SOURCES = daw.c session.c engine.c script.c
SCORE_HEADERS = daw.h session.h engine.h script.h loader.h util.h
HEADERS = engine.h loader.h module.h perone.h util.h
FORMAT_SOURCES = $(filter-out perone.h,$(wildcard *.c *.h test/*.c test/perone/*.c web/*.c web/*.h plugins/*/plugin.h examples/termux_synth/src/*.c))

.PHONY: all test test-plugins test-prog test-brickworks check-plugins run keys prog clean format format-check
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

$(JANET)/build/libjanet.a: $(JANET)/build/c/janet.c
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
TEST_EFFECT = build/effect.perone
$(TEST_BUNDLE)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX): test/perone/plugin.c perone.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -fPIC -fvisibility=hidden -shared $< -o $@

$(TEST_BUNDLE)/product.json: test/perone/product.json
	mkdir -p $(dir $@)
	cp $< $@

$(TEST_EFFECT)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX): test/perone/plugin.c perone.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -DPERONE_TEST_EFFECT -fPIC -fvisibility=hidden -shared $< -o $@

$(TEST_EFFECT)/product.json: test/perone/effect.json
	mkdir -p $(dir $@)
	cp $< $@

test: build/test build/termux_test build/daw_test build/player_test $(TEST_BUNDLE)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX) $(TEST_BUNDLE)/product.json $(TEST_EFFECT)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX) $(TEST_EFFECT)/product.json
	./build/test
	./build/termux_test
	./build/daw_test
	./build/player_test

test-plugins: check-plugins build/plugins_test
	./build/plugins_test

run: build/host
	./build/host plugins/synth_mono/build/plugin.perone

keys: build/termux_synth
	./build/termux_synth --keys

build/daw: daw_main.c player.c player.h export.c $(SCORE_SOURCES) $(SCORE_HEADERS) loader.c $(HEADERS) audio.h build/audio.o $(SCRIPT) build/daw.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(dir $(MINIAUDIO)) $(SCRIPT_FLAGS) daw_main.c player.c export.c $(SCORE_SOURCES) loader.c build/audio.o $(LDFLAGS) $(SCRIPT_LIBS) $(LDLIBS) -o $@

build/%_test: test/%.c export.c $(SCORE_SOURCES) $(SCORE_HEADERS) loader.c $(HEADERS) audio.h build/audio.o $(SCRIPT) build/daw.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -I$(dir $(MINIAUDIO)) $(SCRIPT_FLAGS) $< export.c $(SCORE_SOURCES) loader.c build/audio.o $(LDFLAGS) $(SCRIPT_LIBS) $(LDLIBS) -o $@

build/player_test: player.c player.h daw_main.c

prog: check-plugins build/daw
	./build/daw examples/prog/polpo.janet build/il_polpo_a_sette_gomiti.wav

test-prog: prog
	./build/daw examples/prog/polpo.janet build/polpo-repeat.wav
	cmp build/polpo-repeat.wav build/il_polpo_a_sette_gomiti.wav

clean:
	rm -f build/audio.o build/json.o build/perone.inc build/daw.inc build/host build/test build/termux_synth build/termux_test build/daw build/daw_test build/plugins_test build/player_test
	rm -rf build/fixture.perone build/effect.perone build/web

# Read-only audit of the bundles built in Brickworks; no plugin compilation here.
BRICKWORKS_PERONE ?= ../brickworks/build/perone
BW_BUNDLES = $(wildcard $(BRICKWORKS_PERONE)/*/build/*.perone)
test-brickworks: build/test
	@test -n "$(BW_BUNDLES)" || { echo "No Perone bundles in $(BRICKWORKS_PERONE)"; exit 1; }
	./build/test $(BW_BUNDLES)

# Web uses the same Janet adapter, scheduler and mixer, with standalone Perone Wasm modules.
EMCC ?= emcc
WEB_FLAGS = -I. $(JANET_INCLUDES) -DJANET_SINGLE_THREADED -DPERONE_PLATFORM='"wasm32"' -DPERONE_SUFFIX='".wasm"'
WEB_SOURCES = $(SCORE_SOURCES) web/host.c web/loader.c
WEB_DEPS = $(WEB_SOURCES) $(SCORE_HEADERS) web/host.h build/daw.inc build/perone.inc
WEB_LINK = -lm --no-entry -sMODULARIZE -sEXPORT_ES6 -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=2097152
WEB_METHODS = "FS","ccall","HEAPU8","HEAPU32","HEAPF32"
WEB_EXPORTS = '["_score_new","_score_free","_score_frames","_score_buffer","_score_render","_score_normalize"]'
PLAYER_FLAGS = -pthread -sAUDIO_WORKLET -sWASM_WORKERS -sASYNCIFY -DMA_ENABLE_AUDIO_WORKLETS -DMA_NO_ENCODING
PLAYER_EXPORTS = '["_score_new","_score_free","_score_player","_player_free","_player_start","_player_stop","_player_status","_player_context","_player_node"]'
.PHONY: web
web: build/web/daw.mjs build/web/player.mjs

$(JANET)/build/c/janet.c: $(JANET)/Makefile
	$(MAKE) -C $(JANET) HOSTCC="$(CC)" build/c/janet.c

# Shared-memory objects are separate from the offline runtime's objects.
build/web/player-janet.o build/web/player-json.o: WEB_THREAD_FLAGS = -pthread
build/web/janet.o build/web/player-janet.o: $(JANET)/build/c/janet.c
	mkdir -p $(dir $@)
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(WEB_FLAGS) $(WEB_THREAD_FLAGS) -c $< -o $@

build/web/json.o build/web/player-json.o: $(SPORK)/src/json.c $(JANET)/build/c/janet.c
	mkdir -p $(dir $@)
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(WEB_FLAGS) $(WEB_THREAD_FLAGS) -DJANET_ENTRY_NAME=janet_json -c $< -o $@

build/web/daw.mjs: $(WEB_DEPS) build/web/janet.o build/web/json.o Makefile
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(WEB_FLAGS) $(WEB_SOURCES) build/web/janet.o build/web/json.o $(WEB_LINK) \
	    -sENVIRONMENT=web,worker,node -sEXPORTED_FUNCTIONS=$(WEB_EXPORTS) -sEXPORTED_RUNTIME_METHODS='[$(WEB_METHODS)]' -o $@

# Miniaudio 0.11.25 loses its worklet stack pointer and uses the unaligned deallocator.
# Patch only the generated web copy; keep the downloaded/native header intact.
build/web/miniaudio.h: $(MINIAUDIO) web/miniaudio.patch
	mkdir -p $(dir $@)
	cp $< $@.tmp
	patch --silent $@.tmp web/miniaudio.patch
	mv $@.tmp $@

build/web/player.mjs: $(WEB_DEPS) player.c player.h web/player.c web/runtime.js web/audio.js audio.c audio.h build/web/miniaudio.h build/web/player-janet.o build/web/player-json.o Makefile
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(WEB_FLAGS) $(PLAYER_FLAGS) -Ibuild/web $(WEB_SOURCES) player.c web/player.c audio.c \
	    build/web/player-janet.o build/web/player-json.o $(WEB_LINK) --post-js web/runtime.js --js-library web/audio.js \
	    -sENVIRONMENT=web,worker,worklet -sEXPORTED_FUNCTIONS=$(PLAYER_EXPORTS) \
	    -sEXPORTED_RUNTIME_METHODS='[$(WEB_METHODS),"emscriptenGetAudioObject"]' -o $@

# Test fixtures implement the same standalone ABI, without a Tibia checkout.
WEB_FIXTURE_FLAGS = -O2 -UNDEBUG -I. --no-entry -sSTANDALONE_WASM -sPURE_WASI -sMALLOC=emmalloc -sALLOW_MEMORY_GROWTH -Wl,--export=__wasm_call_ctors,--export=perone_get_api,--export=malloc,--export=free,--export=calloc,--export=realloc,--export-table,--growable-table
$(TEST_BUNDLE)/wasm32/fixture.wasm: test/perone/plugin.c perone.h
	mkdir -p $(dir $@)
	$(EMCC) $(WEB_FIXTURE_FLAGS) $< -o $@

$(TEST_EFFECT)/wasm32/fixture.wasm: test/perone/plugin.c perone.h
	mkdir -p $(dir $@)
	$(EMCC) $(WEB_FIXTURE_FLAGS) -DPERONE_TEST_EFFECT $< -o $@

.PHONY: test-web
test-web: web build/daw $(TEST_BUNDLE)/product.json $(TEST_EFFECT)/product.json $(TEST_BUNDLE)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX) $(TEST_EFFECT)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX) $(TEST_BUNDLE)/wasm32/fixture.wasm $(TEST_EFFECT)/wasm32/fixture.wasm
	node test/request.mjs
	node test/web.mjs

.PHONY: test-browser
test-browser: test-web
	node test/browser.mjs

# Production bundles must already contain their separately compiled wasm32 binaries.
.PHONY: test-polpo-web
test-polpo-web: web
	node test/browser.mjs test/polpo.html
