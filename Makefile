CC = cc
CFLAGS ?= -O2 -Wall -Wextra
TARGET_OS ?= $(shell uname -s)
LDLIBS = -lm -pthread
ifneq ($(TARGET_OS),Darwin)
LDLIBS += -ldl
endif
ifeq ($(shell uname -o 2>/dev/null),Android)
LDLIBS += -landroid-spawn
endif
MINIAUDIO ?= .deps/miniaudio.h
JANET_VERSION = 1.42.1
JANET ?= .deps/janet-$(JANET_VERSION)
JANET_INCLUDES = -I$(JANET)/src/include -I$(JANET)/src/conf
SPORK ?= .deps/spork
SPORK_REV = 0667f96b74de52747ffe5e19e185563ebf53816b
WEBUI_REV = 52f9e75b92faf9a23fd150b3c60051c4ec85fc69
CLAP_VERSION = 1.2.10
CLAP ?= .deps/clap-$(CLAP_VERSION)
VST3_REV = 93a116f7c6c52a317ec3aaee9b13789068b5234e
VST3 ?= .deps/vst3-c-api-$(VST3_REV)
LV2_DEPS ?= .deps/lv2
LV2_INCLUDES = -I$(LV2_DEPS)/include -I$(LV2_DEPS)/include/lilv-0
LV2_LIBS = $(addprefix $(LV2_DEPS)/lib/,liblilv-0.a libsratom-0.a libsord-0.a libserd-0.a libzix-0.a)
WEBUI ?= .deps/webui-$(WEBUI_REV)
PERONE_PLATFORM ?= $(shell uname -m)-$(shell echo $(TARGET_OS) | tr A-Z a-z)
PERONE_SUFFIX ?= .so
SCRIPT_FLAGS = $(JANET_INCLUDES) -DPERONE_SUFFIX='"$(PERONE_SUFFIX)"' -DPERONE_PLATFORM='"$(PERONE_PLATFORM)"'
PLUGIN_SOURCES = plugin.c script.c trace.c
SCORE_SOURCES = score_janet.c score.c score_view.c score_automation.c session.c sequence.c $(PLUGIN_SOURCES)
VIEW_SOURCES = score_view_json.c json_write.c transport.c
HOST_SOURCES = host/dsp.c host/module.c host/perone.c host/clap.c host/clap_info.c host/lv2.c host/lv2_info.c host/vst3.c host/vst3_info.c host/vst3_host.c
HOST_PLATFORM_SOURCES = posix/host.c posix/clap.c posix/lv2.c posix/vst3.c x11/info.c
HOST_OBJECTS = $(addprefix build/obj/native/,$(HOST_SOURCES:.c=.o) $(HOST_PLATFORM_SOURCES:.c=.o))
UI_OBJECTS = $(addprefix build/obj/native/,x11/ui.o x11/perone_ui.o x11/clap_ui.o x11/lv2_ui.o x11/vst3_ui.o x11/window.o)
PLUGIN_OBJECTS = $(HOST_OBJECTS) $(addprefix build/obj/native/,plugin.o posix/discovery.o json_write.o script.o trace.o json.o janet.o)
SCORE_OBJECTS = $(addprefix build/obj/native/,score_janet.o score.o score_view.o score_automation.o session.o sequence.o) $(PLUGIN_OBJECTS)
VIEW_OBJECTS = $(VIEW_SOURCES:%.c=build/obj/native/%.o)
NATIVE_PROGRAMS = build/cli build/gui build/tools/perone-host build/tools/plugin-scan
NATIVE_TESTS = $(addprefix build/test/,dsp daw player routing score_view plugins ui view_json sequence clap lv2 vst3)
FORMAT_SOURCES = $(filter-out perone.h perone_ui.h,$(wildcard *.c *.h host/*.c host/*.h posix/*.c posix/*.h x11/*.c x11/*.h tools/*.c test/*.c test/*.h test/perone/*.c test/clap/*.c test/lv2/*.c test/vst3/*.c web/*.c web/*.h plugins/*/*.h plugins/*/test.c))

.PHONY: all cli gui tools clean format format-check
all: cli
cli: build/cli
gui: build/gui
tools: build/tools/perone-host build/tools/plugin-scan

format:
	clang-format -i $(FORMAT_SOURCES)

format-check:
	clang-format --dry-run --Werror $(FORMAT_SOURCES)

# Downloads are cached independently of generated files and build products.
.deps/miniaudio.h:
	mkdir -p .deps
	curl -fL --retry 2 https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h -o $@.tmp
	mv $@.tmp $@


# CLAP is a header-only ABI; adapters stay outside the portable core.
$(CLAP)/include/clap/clap.h:
	git clone --depth 1 --branch $(CLAP_VERSION) https://github.com/free-audio/clap.git $(CLAP)


$(JANET)/Makefile:
	git clone --depth 1 --branch v$(JANET_VERSION) https://github.com/janet-lang/janet.git $(JANET)

$(JANET)/build/c/janet.c: $(JANET)/Makefile
	$(MAKE) -C $(JANET) HOSTCC="$(CC)" build/c/janet.c

# Janet 1.42.1 does not mark top-level dynamic bindings during collection.
# Use the same corrected runtime for native and Wasm; leave the download untouched.
build/generated/janet.c: $(JANET)/build/c/janet.c patches/janet.patch Makefile
	mkdir -p $(dir $@)
	cp $< $@.tmp
	patch --silent $@.tmp patches/janet.patch
	mv $@.tmp $@

$(SPORK)/src/json.c:
	mkdir -p $(dir $@)
	curl -fL --retry 2 https://raw.githubusercontent.com/janet-lang/spork/$(SPORK_REV)/src/json.c -o $@.tmp
	mv $@.tmp $@

build/generated/%.inc: lib/%.janet
	mkdir -p $(dir $@)
	sed -e 's/[\\"]/\\&/g' -e 's/^/"/' -e 's/$$/\\n"/' $< > $@

# Native programs share objects, with compiler-generated header dependencies.
build/obj/native/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. -I$(dir $(MINIAUDIO)) $(SCRIPT_FLAGS) -MMD -MP -c $< -o $@

build/obj/native/json.o: $(SPORK)/src/json.c Makefile | $(JANET)/Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(JANET_INCLUDES) -DJANET_ENTRY_NAME=janet_json -MMD -MP -c $< -o $@

build/obj/native/janet.o: build/generated/janet.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(JANET_INCLUDES) -MMD -MP -c $< -o $@

CLAP_OBJECTS = $(addprefix build/obj/native/,host/clap.o host/clap_info.o posix/clap.o x11/clap_ui.o x11/info.o tools/plugin-scan.o)
$(CLAP_OBJECTS): override CPPFLAGS += -I$(CLAP)/include
$(CLAP_OBJECTS): $(CLAP)/include/clap/clap.h

# The official C ABI header is sufficient; no C++ SDK sources or libraries.
$(VST3)/vst3_c_api.h $(VST3)/LICENSE.txt:
	mkdir -p $(VST3)
	curl -fL --retry 2 https://raw.githubusercontent.com/steinbergmedia/vst3_c_api/$(VST3_REV)/$(notdir $@) -o $@.tmp
	mv $@.tmp $@
VST3_OBJECTS = $(addprefix build/obj/native/,host/vst3.o host/vst3_info.o host/vst3_host.o posix/vst3.o x11/vst3_ui.o x11/info.o tools/plugin-scan.o test/vst3.o)
$(VST3_OBJECTS): override CPPFLAGS += -I$(VST3)
$(VST3_OBJECTS): $(VST3)/vst3_c_api.h $(VST3)/LICENSE.txt

$(LV2_DEPS)/.ready: tools/lv2-deps.sh
	CC="$(CC)" sh tools/lv2-deps.sh "$(LV2_DEPS)"

LV2_OBJECTS = $(addprefix build/obj/native/,host/lv2.o host/lv2_info.o posix/lv2.o x11/lv2_ui.o x11/info.o tools/plugin-scan.o test/lv2.o)
$(LV2_OBJECTS): override CPPFLAGS += $(LV2_INCLUDES)
$(LV2_OBJECTS): $(LV2_DEPS)/.ready
$(filter-out build/test/score_view,$(NATIVE_PROGRAMS) $(NATIVE_TESTS)): $(LV2_DEPS)/.ready
$(filter-out build/test/score_view,$(NATIVE_PROGRAMS) $(NATIVE_TESTS)): private LDLIBS += $(LV2_LIBS)

build/obj/native/test/%.o: override CFLAGS += -UNDEBUG
$(addprefix build/obj/native/,score_janet.o script.o trace.o posix/files.o posix/prepare.o tools/perone-host.o test/dsp.o test/daw.o test/routing.o test/plugins.o test/ui.o): | $(JANET)/Makefile
build/obj/native/script.o: build/generated/perone.inc build/generated/plugin.inc
build/obj/native/posix/files.o: build/generated/library.inc
build/obj/native/score_janet.o: build/generated/daw.inc
$(addprefix build/obj/native/,audio.o player.o posix/cli.o posix/export.o posix/gui.o tools/perone-host.o test/player.o): $(MINIAUDIO)
$(addprefix build/obj/native/,posix/gui.o posix/controls.o posix/assets.o): override CPPFLAGS += -I$(WEBUI)/include
$(addprefix build/obj/native/,posix/gui.o posix/controls.o posix/assets.o): $(WEBUI)/include/webui.h

$(NATIVE_PROGRAMS) $(NATIVE_TESTS):
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.o,$^) $(LDLIBS) -o $@

build/cli: build/obj/native/posix/cli.o build/obj/native/player.o build/obj/native/posix/export.o $(SCORE_OBJECTS) build/obj/native/audio.o
build/gui: $(addprefix build/obj/native/,posix/gui.o posix/prepare.o posix/files.o posix/controls.o posix/assets.o player.o audio.o vendor/webui.o vendor/civetweb.o) $(SCORE_OBJECTS) $(VIEW_OBJECTS) $(UI_OBJECTS)
build/tools/perone-host: build/obj/native/tools/perone-host.o $(PLUGIN_OBJECTS) build/obj/native/audio.o
build/gui build/test/ui build/test/clap build/test/lv2 build/test/vst3: private LDLIBS += -lX11

build/tools/plugin-scan: build/obj/native/tools/plugin-scan.o $(HOST_OBJECTS) build/obj/native/json_write.o
$(filter-out build/tools/plugin-scan,$(NATIVE_PROGRAMS)) $(filter-out build/test/score_view,$(NATIVE_TESTS)): | build/tools/plugin-scan


# Only the GUI needs WebUI. Compile its two sources once, without an unused shared library.
$(WEBUI)/include/webui.h:
	git init $(WEBUI)
	git -C $(WEBUI) fetch --depth 1 https://github.com/webui-dev/webui.git $(WEBUI_REV)
	git -C $(WEBUI) checkout $(WEBUI_REV)

build/obj/native/vendor/webui.o: $(WEBUI)/include/webui.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -w -DNO_SSL -I$(WEBUI)/include -MMD -MP -c $(WEBUI)/src/webui.c -o $@

build/obj/native/vendor/civetweb.o: $(WEBUI)/include/webui.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -w -DNDEBUG -DNO_SSL -DNO_CACHING -DNO_CGI -DUSE_WEBSOCKET -I$(WEBUI)/src/civetweb -MMD -MP -c $(WEBUI)/src/civetweb/civetweb.c -o $@

ifeq ($(TARGET_OS),Darwin)
build/gui: build/obj/native/vendor/wkwebview.o
build/gui: LDLIBS += -framework Cocoa -framework WebKit
build/obj/native/vendor/wkwebview.o: $(WEBUI)/include/webui.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -w -MMD -MP -c $(WEBUI)/src/webview/wkwebview.m -o $@
endif

# Tests link only the components they exercise. Fixtures are independent of Tibia.
$(NATIVE_TESTS): build/test/%: build/obj/native/test/%.o
build/test/dsp: $(PLUGIN_OBJECTS)
build/test/ui: $(SCORE_OBJECTS)
build/test/routing: build/obj/native/score.o build/obj/native/session.o build/obj/native/sequence.o $(PLUGIN_OBJECTS)
build/test/daw build/test/player build/test/plugins build/test/view_json build/test/sequence: $(SCORE_OBJECTS)
build/test/daw build/test/player: build/obj/native/posix/export.o build/obj/native/audio.o
build/test/score_view: build/obj/native/score_view.o build/obj/native/score_automation.o
build/test/view_json: $(VIEW_OBJECTS)
build/test/player: $(VIEW_OBJECTS)
build/test/sequence: build/obj/native/snapshot.o

TEST_BUNDLE = build/test/fixture.perone
TEST_EFFECT = build/test/effect.perone
NATIVE_FIXTURES = $(TEST_BUNDLE)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX) $(TEST_EFFECT)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX) $(TEST_BUNDLE)/product.json $(TEST_EFFECT)/product.json
$(TEST_BUNDLE)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX): test/perone/plugin.c perone.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -fPIC -fvisibility=hidden -shared $< -o $@

$(TEST_BUNDLE)/product.json: test/perone/product.json
	mkdir -p $(dir $@)
	cp $< $@

$(TEST_EFFECT)/$(PERONE_PLATFORM)/fixture$(PERONE_SUFFIX): test/perone/plugin.c perone.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -DPERONE_TEST_EFFECT -fPIC -fvisibility=hidden -shared $< -o $@

$(TEST_EFFECT)/product.json: test/perone/effect.json
	mkdir -p $(dir $@)
	cp $< $@

MIXER_FIXTURE = build/test/mixer$(PERONE_SUFFIX)
$(MIXER_FIXTURE): test/perone/mixer.c perone.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. -fPIC -fvisibility=hidden -shared $< -o $@

build/test/fixture.clap: test/clap/plugin.c $(CLAP)/include/clap/clap.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I$(CLAP)/include -fPIC -shared $< -lX11 -o $@

.PHONY: test-clap test-clap-ui
test-clap: build/test/clap build/test/fixture.clap
	./build/test/clap
test-clap-ui: build/test/clap build/test/fixture.clap
	./build/test/clap --ui
build/test/clap: $(SCORE_OBJECTS) $(UI_OBJECTS)

.PHONY: test-clap-editor
test-clap-editor: gui build/test/fixture.clap
	node test/plugin-editor.mjs clap

build/test/fixture.lv2/fixture.so: test/lv2/plugin.c Makefile $(LV2_DEPS)/.ready
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG $(LV2_INCLUDES) -fPIC -shared $< -lX11 -o $@
build/test/fixture.lv2/%.ttl: test/lv2/%.ttl
	mkdir -p $(@D)
	cp $< $@
LV2_FIXTURE = build/test/fixture.lv2/fixture.so build/test/fixture.lv2/manifest.ttl build/test/fixture.lv2/plugin.ttl
build/test/lv2: $(SCORE_OBJECTS) $(UI_OBJECTS)
.PHONY: test-lv2 test-lv2-ui test-lv2-editor
test-lv2-editor: gui $(LV2_FIXTURE)
	node test/plugin-editor.mjs lv2
test-lv2: build/test/lv2 $(LV2_FIXTURE)
	./build/test/lv2
test-lv2-ui: build/test/lv2 $(LV2_FIXTURE)
	./build/test/lv2 --ui

VST3_FIXTURE = build/test/fixture.vst3/Contents/$(shell uname -m)-linux/fixture.so
$(VST3_FIXTURE): test/vst3/plugin.c host/vst3_api.h $(VST3)/vst3_c_api.h Makefile build/config/native.compile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -I$(VST3) -fPIC -shared $< -lX11 -lm -o $@
build/test/vst3: $(SCORE_OBJECTS) $(UI_OBJECTS)
.PHONY: test-vst3 test-vst3-ui test-vst3-editor
test-vst3: build/test/vst3 $(VST3_FIXTURE)
	./build/test/vst3
test-vst3-ui: build/test/vst3 $(VST3_FIXTURE)
	./build/test/vst3 --ui
test-vst3-editor: gui $(VST3_FIXTURE)
	node test/plugin-editor.mjs vst3

.PHONY: test test-plugins test-ui test-editor test-prog test-brickworks check-plugins prog
test: $(addprefix build/test/,dsp daw player routing score_view sequence) $(NATIVE_FIXTURES) $(MIXER_FIXTURE)
	./build/test/dsp
	./build/test/daw
	./build/test/routing
	./build/test/player
	./build/test/score_view
	./build/test/sequence

TEST_PLUGINS = $(addsuffix /build/plugin.perone,$(addprefix plugins/,synth_mono fx_svf tibia_test shape echo drums))
check-plugins:
	@for plugin in $(TEST_PLUGINS); do \
		test -f "$$plugin/product.json" || { echo "Missing $$plugin; build plugins separately: make -C plugins" >&2; exit 1; }; \
	done

test-plugins: check-plugins build/test/plugins
	./build/test/plugins

test-ui: build/test/ui
	./build/test/ui

test-editor: gui test
	node test/editor.mjs

prog: check-plugins cli
	mkdir -p renders
	./build/cli examples/prog/polpo.janet renders/il_polpo_a_sette_gomiti.wav

test-prog: check-plugins cli | build/test
	./build/cli examples/prog/polpo.janet build/test/polpo.wav
	./build/cli examples/prog/polpo.janet build/test/polpo-repeat.wav
	cmp build/test/polpo-repeat.wav build/test/polpo.wav

build/test:
	mkdir -p $@

# Fetch/generate/compile the canonical plugin library independently of the host.
include plugins/library.mk

# The mixer shares the library's pinned headers; the host needs no Tibia/Node build.
MIXER_OBJECTS = build/obj/native/session.o build/obj/web-offline/session.o build/obj/web-player/session.o
$(MIXER_OBJECTS): override CPPFLAGS += -I$(BW_SOURCE)/include
$(MIXER_OBJECTS): $(BW_SOURCE)/.ready plugins/library.mk

# Read-only audit of the bundles; no plugin compilation here.
BW_BUNDLES = $(wildcard $(BRICKWORKS_PERONE)/*/build/*.perone)
test-brickworks: build/test/dsp
	@test -n "$(BW_BUNDLES)" || { echo "No Perone bundles in $(BRICKWORKS_PERONE)"; exit 1; }
	./build/test/dsp $(BW_BUNDLES)

.PHONY: test-library-build
test-library-build: library cli build/test/dsp | build/test
	$(NODE) test/library-build.mjs "$(BW_SOURCE)" "$(BRICKWORKS_PERONE)" "$(PERONE_PLATFORM)" "$(MAKE)"

# Wasm variants share sources, but keep separate objects for their memory models.
EMCC ?= emcc
WEB_FLAGS = -I. $(JANET_INCLUDES) -DJANET_SINGLE_THREADED -DPERONE_PLATFORM='"wasm32"' -DPERONE_SUFFIX='".wasm"'
WEB_SOURCES = $(SCORE_SOURCES) $(VIEW_SOURCES) snapshot.c web/host.c web/dsp.c web/discovery.c
WEB_OBJECTS = $(addprefix build/obj/web-offline/,$(WEB_SOURCES:.c=.o) janet.o json.o)
PLAYER_OBJECTS = $(addprefix build/obj/web-player/,$(WEB_SOURCES:.c=.o) player.o web/player_api.o audio.o janet.o json.o)
WEB_LINK = -lm --no-entry -sMODULARIZE -sEXPORT_ES6 -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=2097152
WEB_METHODS = "FS","UTF8ToString","ccall","HEAPU8","HEAPU32","HEAPF32"
TRANSPORT_EXPORTS = "_transport_new","_transport_free_web","_transport_attach_web","_transport_started","_transport_stopped","_transport_seeked","_transport_collect","_transport_detach","_transport_json_web"
VIEW_EXPORTS = "_score_prepare","_score_describe","_score_pack_web","_score_pack_length","_score_import","_web_score_activate","_malloc","_score_take_view","_view_free","_score_view_json","_free",$(TRANSPORT_EXPORTS)
SCORE_EXPORTS = "_score_new","_web_score_free","_score_listen","_score_duration","_score_can_seek"
WEB_EXPORTS = '[$(VIEW_EXPORTS),$(SCORE_EXPORTS),"_score_frames","_score_buffer","_score_render","_score_normalize"]'
PLAYER_FLAGS = -pthread -sWASM_WORKERS -DMA_ENABLE_AUDIO_WORKLETS -DMA_NO_ENCODING
PLAYER_EXPORTS = '[$(VIEW_EXPORTS),$(SCORE_EXPORTS),"_score_dsp","_player_time","_score_player","_transport_update_web","_player_free","_player_start","_player_stop","_player_pause","_player_seek","_player_sync","_player_status","_player_context","_player_node"]'
# Publish C Brickworks examples only; fxpp_* and synthpp_* are duplicate C++ variants.
WEB_CONTENT ?= lib examples $(wildcard plugins/*/build/*.perone \
    $(BRICKWORKS_PERONE)/fx_*/build/*.perone $(BRICKWORKS_PERONE)/synth_*/build/*.perone \
    ../asid/plugin/perone/build/asid.perone ../tibia/out/perone/c/build/tibia-test.perone)

.PHONY: web web-offline
web: build/web/player.mjs
	node web/catalog.mjs build/web/project.json $(WEB_CONTENT)

web-offline: build/web/offline.mjs

build/obj/web-offline/%.o: %.c Makefile | $(JANET)/Makefile
	mkdir -p $(dir $@)
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(WEB_FLAGS) -MMD -MP -c $< -o $@

build/obj/web-player/%.o: %.c Makefile | $(JANET)/Makefile
	mkdir -p $(dir $@)
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(WEB_FLAGS) -MMD -MP -c $< -o $@

$(PLAYER_OBJECTS): override WEB_FLAGS += $(PLAYER_FLAGS) -Ibuild/generated/web
build/obj/web-offline/score_janet.o build/obj/web-player/score_janet.o: build/generated/daw.inc
build/obj/web-offline/script.o build/obj/web-player/script.o: build/generated/perone.inc build/generated/plugin.inc

build/obj/web-offline/janet.o build/obj/web-player/janet.o: build/generated/janet.c Makefile
	mkdir -p $(dir $@)
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(WEB_FLAGS) -MMD -MP -c $< -o $@

build/obj/web-offline/json.o build/obj/web-player/json.o: $(SPORK)/src/json.c Makefile | $(JANET)/Makefile
	mkdir -p $(dir $@)
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(WEB_FLAGS) -DJANET_ENTRY_NAME=janet_json -MMD -MP -c $< -o $@

build/web/offline.mjs: $(WEB_OBJECTS) Makefile
	mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) $(LDFLAGS) $(WEB_OBJECTS) $(WEB_LINK) -sENVIRONMENT=web,worker,node \
	    -sEXPORTED_FUNCTIONS=$(WEB_EXPORTS) -sEXPORTED_RUNTIME_METHODS='[$(WEB_METHODS)]' -o $@

# Miniaudio 0.11.25 loses its worklet stack pointer and uses the unaligned deallocator.
# Patch only the generated web copy; keep the downloaded/native header intact.
build/generated/web/miniaudio.h: $(MINIAUDIO) patches/miniaudio.patch
	mkdir -p $(dir $@)
	cp $< $@.tmp
	patch --silent $@.tmp patches/miniaudio.patch
	mv $@.tmp $@

build/obj/web-player/audio.o build/obj/web-player/player.o build/obj/web-player/web/player_api.o: build/generated/web/miniaudio.h
build/web/player.mjs: $(PLAYER_OBJECTS) web/runtime.js patches/emscripten.js Makefile
	mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) $(LDFLAGS) $(PLAYER_FLAGS) $(PLAYER_OBJECTS) $(WEB_LINK) -sAUDIO_WORKLET -sASYNCIFY \
	    --post-js web/runtime.js --js-library patches/emscripten.js -sENVIRONMENT=web,worker,worklet \
	    -sEXPORTED_FUNCTIONS=$(PLAYER_EXPORTS) -sEXPORTED_RUNTIME_METHODS='[$(WEB_METHODS),"emscriptenGetAudioObject"]' -o $@

-include $(wildcard build/obj/*/*.d build/obj/*/*/*.d)

# Test fixtures implement the same standalone ABI, without a Tibia checkout.
WEB_FIXTURE_FLAGS = -O2 -UNDEBUG -I. --no-entry -sSTANDALONE_WASM -sPURE_WASI -sMALLOC=emmalloc -sALLOW_MEMORY_GROWTH -Wl,--export=__wasm_call_ctors,--export=perone_get_api,--export=malloc,--export=free,--export=calloc,--export=realloc,--export-table,--growable-table
WEB_FIXTURES = $(TEST_BUNDLE)/wasm32/fixture.wasm $(TEST_EFFECT)/wasm32/fixture.wasm
$(TEST_BUNDLE)/wasm32/fixture.wasm: test/perone/plugin.c perone.h Makefile
	mkdir -p $(dir $@)
	$(EMCC) $(WEB_FIXTURE_FLAGS) $< -o $@

$(TEST_EFFECT)/wasm32/fixture.wasm: test/perone/plugin.c perone.h Makefile
	mkdir -p $(dir $@)
	$(EMCC) $(WEB_FIXTURE_FLAGS) -DPERONE_TEST_EFFECT $< -o $@

.PHONY: test-web test-browser test-polpo-web test-trace test-editor-web test-editor-ui test-parameter-ui test-library test-live
.PHONY: test-build test-build-web
test-build: $(MINIAUDIO) | build/test
	node test/build.mjs "$(MAKE)" "$(CC)" "$(MINIAUDIO)"

test-build-web: $(MINIAUDIO) $(JANET)/Makefile | build/test
	node test/build.mjs "$(MAKE)" "$(CC)" "$(MINIAUDIO)" "$(EMCC)"

test-web: build/web/offline.mjs build/web/player.mjs cli build/test/view_json $(NATIVE_FIXTURES) $(WEB_FIXTURES)
	node test/catalog.mjs
	node test/request.mjs
	node test/web.mjs
	node test/perone-controls.mjs
	node test/view-json.mjs

test-browser: test-web
	node test/browser.mjs

test-live: gui test-web
	node test/live-editor.mjs

# Production bundles must already contain their separately compiled wasm32 binaries.
test-polpo-web: build/web/offline.mjs build/web/player.mjs
	node test/browser.mjs test/polpo.html

test-trace: test-web
	node test/trace.mjs

test-editor-web: web | build/test
	node test/editor-web.mjs

# The published library must have both native and Wasm versions of its production bundles.
test-library: gui web | build/test
	node test/library.mjs

# Self-contained UI fixture: the exact same ES module drives native and Wasm DSPs.
test-editor-ui: gui test-web test-parameter-ui
	node test/editor-ui.mjs
	node test/automation.mjs

# Shared parameter controls can be checked without compiling either audio backend.
test-parameter-ui:
	node test/parameters.mjs
	node test/parameter-ui.mjs

.PHONY: test-score-space test-graph
test-score-space: gui build/web/player.mjs $(NATIVE_FIXTURES) $(WEB_FIXTURES) | build/test
	node test/score-space.mjs

test-graph: gui build/web/player.mjs $(NATIVE_FIXTURES) $(WEB_FIXTURES) | build/test
	node test/graph.mjs

clean:
	rm -rf build

# Recheck configuration on each invocation; unchanged files keep their timestamps.
# Capture global flags here so per-target additions cannot leak through prerequisites.
build/config/native.compile: private CONFIG := $(CC) | $(CPPFLAGS) | $(CFLAGS) | $(SCRIPT_FLAGS) | $(MINIAUDIO) | $(SPORK) | $(WEBUI) | $(CLAP) | $(VST3) | $(LV2_DEPS) | $(BW_SOURCE)
build/config/native.link: private CONFIG := $(CC) | $(CFLAGS) | $(LDFLAGS) | $(LDLIBS) | $(TARGET_OS)
build/config/web-offline.compile: private CONFIG := $(EMCC) | $(CPPFLAGS) | $(CFLAGS) | $(WEB_FLAGS) | $(SPORK) | $(BW_SOURCE)
build/config/web-player.compile: private CONFIG := $(EMCC) | $(CPPFLAGS) | $(CFLAGS) | $(WEB_FLAGS) | $(PLAYER_FLAGS) | $(MINIAUDIO) | $(SPORK) | $(BW_SOURCE)
build/config/web-offline.link: private CONFIG := $(EMCC) | $(CFLAGS) | $(LDFLAGS) | $(WEB_LINK) | $(WEB_EXPORTS) | $(WEB_METHODS)
build/config/web-player.link: private CONFIG := $(EMCC) | $(CFLAGS) | $(LDFLAGS) | $(WEB_LINK) | $(PLAYER_FLAGS) | $(PLAYER_EXPORTS) | $(WEB_METHODS)
build/config/web-fixtures: private CONFIG := $(EMCC) | $(WEB_FIXTURE_FLAGS)
build/config/web-audio: private CONFIG := $(MINIAUDIO)
build/config/janet: private CONFIG := $(JANET)
CONFIG_FILES = $(addprefix build/config/,native.compile native.link web-offline.compile web-offline.link web-player.compile web-player.link web-fixtures web-audio janet)
quote = '$(subst ','"'"',$(1))'
.PHONY: FORCE
FORCE:
$(CONFIG_FILES): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' $(call quote,$(CONFIG)) > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

NATIVE_OBJECTS = $(addprefix build/obj/native/,$(patsubst %.c,%.o,$(wildcard *.c host/*.c posix/*.c x11/*.c tools/*.c test/*.c)) json.o janet.o vendor/webui.o vendor/civetweb.o vendor/wkwebview.o)
$(NATIVE_OBJECTS) $(filter %$(PERONE_SUFFIX),$(NATIVE_FIXTURES)) $(MIXER_FIXTURE) build/test/fixture.clap build/test/fixture.lv2/fixture.so: build/config/native.compile
$(NATIVE_PROGRAMS) $(NATIVE_TESTS): build/config/native.link
$(WEB_OBJECTS): build/config/web-offline.compile
$(PLAYER_OBJECTS): build/config/web-player.compile
build/web/offline.mjs: build/config/web-offline.link
build/web/player.mjs: build/config/web-player.link
$(WEB_FIXTURES): build/config/web-fixtures
build/generated/web/miniaudio.h: build/config/web-audio
build/generated/janet.c: build/config/janet
