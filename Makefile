CC = cc
CFLAGS ?= -O2 -Wall -Wextra
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
TARGET_OS ?= $(shell uname -s)
PLATFORM := $(if $(filter MINGW% MSYS% Windows,$(TARGET_OS)),win32,posix)
override CPPFLAGS += -I$(PLATFORM) -Ibuild/generated
MINIAUDIO ?= .deps/miniaudio.h
JANET_VERSION = 1.42.1
JANET ?= .deps/janet-$(JANET_VERSION)
JANET_INCLUDES = -I$(JANET)/src/include -I$(JANET)/src/conf
SPORK_REV = 8a87c5ae1cbbf3a0037eeac7f172bd45da4d9910
SPORK ?= .deps/spork-$(SPORK_REV)
WEBUI_REV = efac0e24dd4e6199dd7308c485906c90fb8816a2
CLAP_VERSION = 1.2.10
CLAP ?= .deps/clap-$(CLAP_VERSION)
PORTMIDI_VERSION = 2.0.8
PORTMIDI ?= .deps/portmidi-$(PORTMIDI_VERSION)
VST3_REV = 93a116f7c6c52a317ec3aaee9b13789068b5234e
VST3 ?= .deps/vst3-c-api-$(VST3_REV)
LV2_DEPS ?= .deps/lv2
LV2_INCLUDES = -I$(LV2_DEPS)/include -I$(LV2_DEPS)/include/lilv-0 -I$(LV2_DEPS)/include/sratom-0 -I$(LV2_DEPS)/include/serd-0 -I$(LV2_DEPS)/include/sord-0
LV2_LIBS = $(addprefix $(LV2_DEPS)/lib/,liblilv-0.a libsratom-0.a libsord-0.a libserd-0.a libzix-0.a)
WEBUI ?= .deps/webui-$(WEBUI_REV)
include $(PLATFORM)/build.mk
PERONE_SUFFIX ?= .so
include plugins/library.mk
SCRIPT_FLAGS = $(JANET_INCLUDES) -DPERONE_SUFFIX='"$(PERONE_SUFFIX)"' -DPERONE_PLATFORM='"$(PERONE_PLATFORM)"'
PLUGIN_SOURCES = midi.c plugin.c script.c trace.c state.c
SCORE_SOURCES = score_janet.c score.c score_view.c score_automation.c session.c sequence.c $(PLUGIN_SOURCES)
VIEW_SOURCES = score_view_json.c json_write.c transport.c
HOST_SOURCES = host/dsp.c host/module.c host/scan.c host/perone.c host/clap.c host/clap_info.c host/lv2.c host/lv2_info.c host/lv2_state.c host/vst3.c host/vst3_info.c host/vst3_host.c
HOST_PLATFORM_SOURCES = $(addprefix $(PLATFORM)/,host.c files.c thread.c settings.c scan.c clap.c vst3.c) native/lv2.c $(WINDOW_SYSTEM)/info.c
HOST_OBJECTS = $(addprefix build/obj/native/,$(HOST_SOURCES:.c=.o) $(HOST_PLATFORM_SOURCES:.c=.o))
UI_OBJECTS = $(addprefix build/obj/native/,host/ui.o $(WINDOW_SYSTEM)/perone_ui.o host/clap_ui.o host/lv2_ui.o host/vst3_ui.o $(WINDOW_SYSTEM)/window.o)
PLUGIN_OBJECTS = $(HOST_OBJECTS) $(addprefix build/obj/native/,midi.o plugin.o state.o native/discovery.o json_write.o script.o trace.o json.o janet.o)
SCORE_OBJECTS = $(addprefix build/obj/native/,score_janet.o score.o score_view.o score_automation.o session.o sequence.o) $(PLUGIN_OBJECTS)
VIEW_OBJECTS = $(VIEW_SOURCES:%.c=build/obj/native/%.o)
NATIVE_PROGRAMS = build/cli$(EXE) build/gui$(EXE) build/tools/perone-host$(EXE) build/tools/plugin-scan$(EXE)
NATIVE_TESTS = $(addsuffix $(EXE),$(addprefix build/test/,platform dsp daw player routing score_view plugins ui view_json sequence clap lv2 vst3 midi midi_devices))
FORMAT_SOURCES = $(filter-out perone.h perone_ui.h,$(wildcard *.c *.h host/*.c host/*.h native/*.c native/*.h win32/*.c win32/*.h test/posix/*.c test/posix/*.h test/x11/*.c test/x11/*.h test/win32/*.c test/win32/*.h posix/*.c posix/*.h gtk/*.c x11/*.c x11/*.h tools/*.c test/*.c test/*.cpp test/*.h test/perone/*.c test/clap/*.c test/lv2/*.c test/vst3/*.c web/*.c web/*.h plugins/*/*.h plugins/*/*.cpp plugins/*/test.c))

.PHONY: all cli gui tools clean format format-check
all: cli
cli: build/cli$(EXE)
gui: build/gui$(EXE)
tools: build/tools/perone-host$(EXE) build/tools/plugin-scan$(EXE)

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


$(PORTMIDI)/CMakeLists.txt:
	git clone --depth 1 --branch v$(PORTMIDI_VERSION) https://github.com/PortMidi/portmidi.git $(PORTMIDI)

$(PORTMIDI)/build/libportmidi.a: $(PORTMIDI)/CMakeLists.txt
	cmake -S $(PORTMIDI) -B $(PORTMIDI)/build -G Ninja -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_PORTMIDI_TESTS=OFF -DBUILD_JAVA_NATIVE_INTERFACE=OFF
	cmake --build $(PORTMIDI)/build --target portmidi

$(addprefix build/obj/native/,native/midi.o test/midi_devices.o): override CPPFLAGS += -I$(PORTMIDI)/pm_common
$(addprefix build/obj/native/,native/midi.o test/midi_devices.o): $(PORTMIDI)/CMakeLists.txt $(MINIAUDIO)
build/gui$(EXE) build/test/midi_devices$(EXE): private LDLIBS += $(MIDI_LIBS)


$(JANET)/Makefile:
	git clone --depth 1 --branch v$(JANET_VERSION) https://github.com/janet-lang/janet.git $(JANET)

$(JANET)/build/c/janet.c: $(JANET)/Makefile
	$(MAKE) -C $(JANET) HOSTCC="$(CC)" build/c/janet.c

# Janet 1.42.1: keep dynamic bindings alive and avoid NaN-to-integer casts.
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

CLAP_OBJECTS = $(addprefix build/obj/native/,host/clap.o host/clap_info.o $(PLATFORM)/clap.o host/clap_ui.o $(WINDOW_SYSTEM)/window.o $(WINDOW_SYSTEM)/info.o tools/plugin-scan.o test/clap.o)
$(CLAP_OBJECTS): override CPPFLAGS += -I$(CLAP)/include
$(CLAP_OBJECTS): $(CLAP)/include/clap/clap.h

# The official C ABI header is sufficient; no C++ SDK sources or libraries.
$(VST3)/vst3_c_api.h $(VST3)/LICENSE.txt:
	mkdir -p $(VST3)
	curl -fL --retry 2 https://raw.githubusercontent.com/steinbergmedia/vst3_c_api/$(VST3_REV)/$(notdir $@) -o $@.tmp
	mv $@.tmp $@
VST3_OBJECTS = $(addprefix build/obj/native/,host/vst3.o host/vst3_info.o host/vst3_host.o $(PLATFORM)/vst3.o host/vst3_ui.o $(WINDOW_SYSTEM)/info.o tools/plugin-scan.o test/vst3.o)
$(VST3_OBJECTS): override CPPFLAGS += -I$(VST3)
$(VST3_OBJECTS): $(VST3)/vst3_c_api.h $(VST3)/LICENSE.txt

$(LV2_DEPS)/.ready: tools/lv2-deps.sh
	CC="$(CC)" sh tools/lv2-deps.sh "$(LV2_DEPS)"

LV2_OBJECTS = $(addprefix build/obj/native/,host/lv2.o host/lv2_info.o host/lv2_state.o native/lv2.o host/lv2_ui.o $(WINDOW_SYSTEM)/info.o tools/plugin-scan.o test/lv2.o)
$(LV2_OBJECTS): override CPPFLAGS += $(LV2_INCLUDES) -DLILV_STATIC -DSERD_STATIC -DSRATOM_STATIC
$(LV2_OBJECTS): $(LV2_DEPS)/.ready
$(filter-out build/test/score_view$(EXE),$(NATIVE_PROGRAMS) $(NATIVE_TESTS)): $(LV2_DEPS)/.ready
$(filter-out build/test/score_view$(EXE),$(NATIVE_PROGRAMS) $(NATIVE_TESTS)): private LDLIBS += $(LV2_LIBS)

build/obj/native/test/%.o: override CFLAGS += -UNDEBUG
$(addprefix build/obj/native/,score_janet.o script.o trace.o native/files.o native/prepare.o tools/perone-host.o test/dsp.o test/daw.o test/routing.o test/plugins.o test/ui.o): | $(JANET)/Makefile
build/obj/native/script.o: build/generated/perone.inc build/generated/plugin.inc
build/obj/native/native/files.o: build/generated/library.inc
build/obj/native/score_janet.o: build/generated/daw.inc
$(addprefix build/obj/native/,audio.o player.o native/cli.o native/export.o native/gui.o tools/perone-host.o test/player.o): $(MINIAUDIO)
$(addprefix build/obj/native/,native/gui.o native/controls.o native/assets.o test/webview.o): override CPPFLAGS += -I$(WEBUI)/include
$(addprefix build/obj/native/,native/gui.o native/controls.o native/assets.o test/webview.o): $(WEBUI)/include/webui.h

$(NATIVE_PROGRAMS) $(NATIVE_TESTS): $(NATIVE_RESOURCES)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $(NATIVE_LDFLAGS) $(filter %.o,$^) $(filter %.a,$^) $(LDLIBS) -o $@

build/cli$(EXE): build/obj/native/native/cli.o build/obj/native/player.o build/obj/native/native/export.o $(SCORE_OBJECTS) build/obj/native/audio.o
build/gui$(EXE): $(PORTMIDI)/build/libportmidi.a
build/gui$(EXE): $(addprefix build/obj/native/,$(GUI_LOOP) $(WEBVIEW_OBJECTS)) $(GUI_RUNTIME)
build/gui$(EXE): $(addprefix build/obj/native/,native/midi.o native/gui.o native/prepare.o native/files.o native/controls.o native/assets.o player.o audio.o vendor/webui.o vendor/civetweb.o) $(SCORE_OBJECTS) $(VIEW_OBJECTS) $(UI_OBJECTS)
build/tools/perone-host$(EXE): build/obj/native/tools/perone-host.o $(PLUGIN_OBJECTS) build/obj/native/audio.o
build/gui$(EXE) build/test/webview$(EXE): private LDLIBS += $(WEBVIEW_LIBS)
build/gui$(EXE) build/test/ui$(EXE) build/test/clap$(EXE) build/test/lv2$(EXE) build/test/vst3$(EXE): private LDLIBS += $(WINDOW_LIBS)

build/tools/plugin-scan$(EXE): build/obj/native/tools/plugin-scan.o $(HOST_OBJECTS) $(addprefix build/obj/native/,midi.o plugin.o state.o json_write.o)
$(filter-out build/tools/plugin-scan$(EXE),$(NATIVE_PROGRAMS)) $(filter-out build/test/score_view$(EXE),$(NATIVE_TESTS)): | build/tools/plugin-scan$(EXE)


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

.PHONY: test-webview
test-webview: build/test/webview$(EXE)
	./build/test/webview$(EXE)

build/test/webview$(EXE): $(addprefix build/obj/native/,test/webview.o $(GUI_LOOP) $(WEBVIEW_OBJECTS) vendor/webui.o vendor/civetweb.o) $(TEST_RUNTIME) $(NATIVE_RESOURCES) build/config/native.link
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $(NATIVE_LDFLAGS) $(filter %.o,$^) $(LDLIBS) -o $@

ifeq ($(TARGET_OS),Darwin)
build/gui$(EXE) build/test/webview$(EXE): build/obj/native/vendor/wkwebview.o
build/gui$(EXE) build/test/webview$(EXE): LDLIBS += -framework Cocoa -framework WebKit
build/obj/native/vendor/wkwebview.o: $(WEBUI)/include/webui.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -w -MMD -MP -c $(WEBUI)/src/webview/wkwebview.m -o $@
endif

# Tests link only the components they exercise. Fixtures are independent of Tibia.
$(NATIVE_TESTS): build/test/%$(EXE): build/obj/native/test/%.o
build/test/dsp$(EXE): $(PLUGIN_OBJECTS)
build/test/platform$(EXE): $(PLUGIN_OBJECTS) build/obj/native/native/files.o
build/test/ui$(EXE): $(SCORE_OBJECTS)
build/test/routing$(EXE): build/obj/native/score.o build/obj/native/session.o build/obj/native/sequence.o $(PLUGIN_OBJECTS)
build/test/daw$(EXE) build/test/player$(EXE) build/test/plugins$(EXE) build/test/view_json$(EXE) build/test/sequence$(EXE): $(SCORE_OBJECTS)
build/test/daw$(EXE): build/obj/native/test/$(PLATFORM)/export.o
build/test/daw$(EXE) build/test/player$(EXE): build/obj/native/native/export.o build/obj/native/audio.o
build/test/score_view$(EXE): build/obj/native/score_view.o build/obj/native/score_automation.o
build/test/view_json$(EXE): $(VIEW_OBJECTS)
build/test/player$(EXE): $(VIEW_OBJECTS)
build/test/sequence$(EXE): build/obj/native/snapshot.o

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

build/test/fixture.clap: test/clap/plugin.c test/$(WINDOW_SYSTEM)/clap_gui.c test/clap/plugin.h test/$(WINDOW_SYSTEM)/clap_platform.h $(CLAP)/include/clap/clap.h Makefile
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NATIVE_LDFLAGS) -UNDEBUG -I. -I$(CLAP)/include -fPIC -shared $(filter %.c,$^) $(WINDOW_LIBS) -o $@

.PHONY: test-clap test-clap-ui
test-clap: build/test/clap$(EXE) build/test/fixture.clap
	./build/test/clap$(EXE)
test-clap-ui: build/test/clap$(EXE) build/test/fixture.clap
	./build/test/clap$(EXE) --ui
build/test/clap$(EXE): $(SCORE_OBJECTS) $(UI_OBJECTS) $(addprefix build/obj/native/,$(TEST_UI_OBJECTS))

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
build/test/lv2$(EXE): $(SCORE_OBJECTS) $(UI_OBJECTS)
.PHONY: test-lv2 test-lv2-ui test-lv2-editor
test-lv2-editor: gui $(LV2_FIXTURE)
	node test/plugin-editor.mjs lv2
test-lv2: build/test/lv2$(EXE) $(LV2_FIXTURE)
	./build/test/lv2$(EXE)
test-lv2-ui: build/test/lv2$(EXE) $(LV2_FIXTURE)
	./build/test/lv2$(EXE) --ui

VST3_FIXTURE = build/test/fixture.vst3/Contents/$(shell uname -m)-linux/fixture.so
$(VST3_FIXTURE): test/vst3/plugin.c host/vst3_api.h $(VST3)/vst3_c_api.h Makefile build/config/native.compile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -I. -I$(VST3) -fPIC -shared $< -lX11 -lm -o $@
build/test/vst3$(EXE): $(SCORE_OBJECTS) $(UI_OBJECTS)
.PHONY: test-vst3 test-vst3-ui test-vst3-editor
test-vst3: build/test/vst3$(EXE) $(VST3_FIXTURE)
	./build/test/vst3$(EXE)
test-vst3-ui: build/test/vst3$(EXE) $(VST3_FIXTURE)
	./build/test/vst3$(EXE) --ui
test-vst3-editor: gui $(VST3_FIXTURE)
	node test/plugin-editor.mjs vst3

build/test/midi$(EXE): $(SCORE_OBJECTS) build/obj/native/snapshot.o
.PHONY: test-midi
test-midi: build/test/midi$(EXE) $(NATIVE_FIXTURES) build/test/fixture.clap $(LV2_FIXTURE) $(VST3_FIXTURE)
	./build/test/midi$(EXE)

build/test/midi_devices$(EXE): $(SCORE_OBJECTS) $(addprefix build/obj/native/,native/midi.o player.o audio.o) $(PORTMIDI)/build/libportmidi.a
.PHONY: test-midi-devices
test-midi-devices: build/test/midi_devices$(EXE)
	./build/test/midi_devices$(EXE)

.PHONY: test-midi-ui
test-midi-ui: gui test-web
	node test/midi-devices.mjs
	node test/midi-ui.mjs

.PHONY: test test-plugins test-ui test-editor test-polpo test-brickworks check-plugins polpo
test: $(addsuffix $(EXE),$(addprefix build/test/,platform dsp daw player routing score_view sequence)) $(NATIVE_FIXTURES) $(MIXER_FIXTURE)
	./build/test/platform$(EXE)
	./build/test/dsp$(EXE)
	./build/test/daw$(EXE)
	./build/test/routing$(EXE)
	./build/test/player$(EXE)
	./build/test/score_view$(EXE)
	./build/test/sequence$(EXE)

TEST_PLUGINS = $(addsuffix /build/plugin.perone,$(addprefix plugins/,synth_mono fx_svf tibia_test shape echo drums))
check-plugins:
	@for plugin in $(TEST_PLUGINS); do \
		test -f "$$plugin/product.json" || { echo "Missing $$plugin; build plugins separately: make -C plugins" >&2; exit 1; }; \
	done

test-plugins: check-plugins build/test/plugins$(EXE)
	./build/test/plugins$(EXE)

test-ui: build/test/ui$(EXE)
	./build/test/ui$(EXE)

test-editor: gui test test-code
	node test/editor.mjs

polpo: check-plugins cli
	mkdir -p renders
	./build/cli$(EXE) examples/polpo.janet renders/il_polpo_a_sette_gomiti.wav

test-polpo: check-plugins cli | build/test
	./build/cli$(EXE) examples/polpo.janet build/test/polpo.wav
	./build/cli$(EXE) examples/polpo.janet build/test/polpo-repeat.wav
	cmp build/test/polpo-repeat.wav build/test/polpo.wav

build/test:
	mkdir -p $@

MDA_TEST_FLAGS ?= -O1 -g -fwrapv -Wno-multichar -Wno-write-strings

# The mixer shares the library's pinned headers; the host needs no Tibia/Node build.
MIXER_OBJECTS = build/obj/native/session.o build/obj/web-offline/session.o build/obj/web-player/session.o
$(MIXER_OBJECTS): override CPPFLAGS += -I$(BW_SOURCE)/include
$(MIXER_OBJECTS): $(BW_SOURCE)/.ready

# Read-only audit of the bundles; no plugin compilation here.
BW_BUNDLES = $(wildcard $(BRICKWORKS_PERONE)/*/build/*.perone)
test-brickworks: build/test/dsp$(EXE)
	@test -n "$(BW_BUNDLES)" || { echo "No Perone bundles in $(BRICKWORKS_PERONE)"; exit 1; }
	./build/test/dsp$(EXE) $(BW_BUNDLES)

.PHONY: test-library-build
test-library-build: library cli build/test/dsp$(EXE) | build/test
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
PLAYER_EXPORTS = '[$(VIEW_EXPORTS),$(SCORE_EXPORTS),"_score_dsp","_score_state_save","_score_state_load","_player_time","_player_audio_time","_player_active","_player_monitor","_score_player","_transport_update_web","_player_free","_player_start","_player_stop","_player_pause","_player_seek","_player_sync","_player_status","_player_context","_player_node","_player_midi","_player_midi_read","_player_midi_capture","_player_midi_panic"]'
# Exclude Brickworks fxpp_* and synthpp_*: they duplicate the C examples.
WEB_CONTENT ?= lib examples $(wildcard plugins/*/build/*.perone \
    $(BRICKWORKS_PERONE)/fx_*/build/*.perone $(BRICKWORKS_PERONE)/synth_*/build/*.perone $(MDA_PERONE)/*.perone \
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

test-web: build/web/offline.mjs build/web/player.mjs cli build/test/view_json$(EXE) $(NATIVE_FIXTURES) $(WEB_FIXTURES)
	node test/catalog.mjs
	node test/request.mjs
	node test/web.mjs
	node test/plugin-references.mjs
	node test/perone-controls.mjs
	node test/view-json.mjs

test-browser: test-web
	node test/browser.mjs
	node test/editor-loading.mjs

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

.PHONY: test-brickworks-ui
test-brickworks-ui: gui build/web/player.mjs library | build/test
	$(MAKE) -f plugins/library.mk library-build PERONE_PLATFORM=wasm32
	$(MAKE) web
	node test/brickworks-ui.mjs

.PHONY: test-settings
test-settings: gui test-web build/test/fixture.clap
	node test/settings.mjs

# Self-contained UI fixture: the exact same ES module drives native and Wasm DSPs.
test-editor-ui: gui test-web test-parameter-ui
	node test/editor-ui.mjs
	node test/automation.mjs

# Shared parameter controls can be checked without compiling either audio backend.
test-parameter-ui:
	node test/parameters.mjs
	node test/parameter-ui.mjs

.PHONY: test-code
test-code:
	node test/code.mjs

.PHONY: test-score-space test-graph test-mobile
test-score-space: gui build/web/player.mjs $(NATIVE_FIXTURES) $(WEB_FIXTURES) | build/test
	node test/score-space.mjs

test-graph: gui build/web/player.mjs $(NATIVE_FIXTURES) $(WEB_FIXTURES) | build/test
	node test/graph.mjs

test-mobile: gui build/web/player.mjs $(NATIVE_FIXTURES) $(WEB_FIXTURES) | build/test
	node test/mobile.mjs

clean:
	rm -rf build

# Recheck configuration on each invocation; unchanged files keep their timestamps.
# Capture global flags here so per-target additions cannot leak through prerequisites.
build/config/native.compile: private CONFIG := $(CC) | $(CPPFLAGS) | $(CFLAGS) | $(SCRIPT_FLAGS) | $(MINIAUDIO) | $(SPORK) | $(WEBUI) | $(CLAP) | $(VST3) | $(LV2_DEPS) | $(PORTMIDI) | $(BW_SOURCE)
build/config/native.link: private CONFIG := $(CC) | $(CFLAGS) | $(LDFLAGS) | $(NATIVE_LDFLAGS) | $(LDLIBS) | $(TARGET_OS)
build/config/web-offline.compile: private CONFIG := $(EMCC) | $(CPPFLAGS) | $(CFLAGS) | $(WEB_FLAGS) | $(SPORK) | $(BW_SOURCE)
build/config/web-player.compile: private CONFIG := $(EMCC) | $(CPPFLAGS) | $(CFLAGS) | $(WEB_FLAGS) | $(PLAYER_FLAGS) | $(MINIAUDIO) | $(SPORK) | $(BW_SOURCE)
build/config/web-offline.link: private CONFIG := $(EMCC) | $(CFLAGS) | $(LDFLAGS) | $(WEB_LINK) | $(WEB_EXPORTS) | $(WEB_METHODS)
build/config/web-player.link: private CONFIG := $(EMCC) | $(CFLAGS) | $(LDFLAGS) | $(WEB_LINK) | $(PLAYER_FLAGS) | $(PLAYER_EXPORTS) | $(WEB_METHODS)
build/config/web-fixtures: private CONFIG := $(EMCC) | $(WEB_FIXTURE_FLAGS)
build/config/web-audio: private CONFIG := $(MINIAUDIO)
build/config/janet: private CONFIG := $(JANET)
build/config/mda-test.compile: private CONFIG := $(HOST_CXX) | $(MDA_TEST_FLAGS) | $(MDA_INCLUDES)
CONFIG_FILES = $(addprefix build/config/,native.compile native.link web-offline.compile web-offline.link web-player.compile web-player.link web-fixtures web-audio janet mda-test.compile)
quote = '$(subst ','"'"',$(1))'
.PHONY: FORCE
FORCE:
$(CONFIG_FILES): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' $(call quote,$(CONFIG)) > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

NATIVE_OBJECTS = $(addprefix build/obj/native/,$(patsubst %.c,%.o,$(wildcard *.c host/*.c native/*.c $(PLATFORM)/*.c gtk/*.c $(WINDOW_SYSTEM)/*.c tools/*.c test/*.c test/$(PLATFORM)/*.c test/$(WINDOW_SYSTEM)/*.c)) json.o janet.o vendor/webui.o vendor/civetweb.o vendor/wkwebview.o $(WEBVIEW_OBJECTS))
$(NATIVE_OBJECTS): $(PLATFORM)/build.mk
$(NATIVE_OBJECTS) $(filter %$(PERONE_SUFFIX),$(NATIVE_FIXTURES)) $(MIXER_FIXTURE) build/test/fixture.clap build/test/fixture.lv2/fixture.so: build/config/native.compile
$(NATIVE_PROGRAMS) $(NATIVE_TESTS): build/config/native.link
$(WEB_OBJECTS): build/config/web-offline.compile
$(PLAYER_OBJECTS): build/config/web-player.compile
build/web/offline.mjs: build/config/web-offline.link
build/web/player.mjs: build/config/web-player.link
$(WEB_FIXTURES): build/config/web-fixtures
build/generated/web/miniaudio.h: build/config/web-audio
build/generated/janet.c: build/config/janet

# Run after building the native and Wasm library. HOST_CXX may be clang++ for sanitizers.
MDA_TESTS := $(addsuffix $(EXE),$(addprefix build/test/mda/,$(MDA_NAMES)))
$(MDA_TESTS): build/test/mda/%$(EXE): test/mda.cpp plugins/mda/plugin_cxx.h plugins/mda/mda.h $(MDA_GEN)/%/Makefile build/config/mda-test.compile
	mkdir -p "$(@D)"
	$(HOST_CXX) -std=c++17 $(MDA_TEST_FLAGS) $(MDA_INCLUDES) -I$(MDA_GEN)/$* -I$(MDA_GEN)/$*/src \
	    $(if $(filter mdaSpecMeter,$*),-DMDA_METER) -MMD -MP -MF "$@.d" $< -o "$@"

.PHONY: test-mda
test-mda: $(MDA_TESTS) cli gui build/test/dsp$(EXE) build/web/offline.mjs build/web/player.mjs | build/test
	@for test in $(MDA_TESTS); do "$$test" || exit $$?; done
	$(NODE) test/mda.mjs
	$(NODE) test/mda-ui.mjs
-include $(addsuffix .d,$(MDA_TESTS))
