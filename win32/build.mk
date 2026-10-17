# Native MinGW-w64 binaries, built with GNU Make in the MSYS2 UCRT64 shell.
EXE = .exe
WINDOW_SYSTEM = win32
PERONE_SUFFIX ?= .dll
override CPPFLAGS += -Itest/win32
LDLIBS = -lm -lws2_32 -lmswsock -lpsapi -lwinmm -lole32 -luuid -lshlwapi -luser32 -ladvapi32
NATIVE_LDFLAGS = -static
NATIVE_RESOURCES = build/obj/native/win32/app.o
WINDRES ?= windres
WINDOW_LIBS = -luser32 -lgdi32
MIDI_LIBS = -lwinmm
GUI_LOOP = win32/gui_loop.o
WEBVIEW_OBJECTS = vendor/win32_wv2.o
WEBVIEW_LIBS = -lstdc++ -loleaut32 -lcomctl32 -lversion
TEST_UI_OBJECTS = test/win32/ui.o win32/gui_loop.o
GUI_RUNTIME = build/WebView2Loader.dll
TEST_RUNTIME = build/test/WebView2Loader.dll
WEBVIEW2 = .deps/webview2-1.0.3650.58

# UTF-8 also covers CRT argv/getenv and dependencies that use narrow file APIs.
$(NATIVE_RESOURCES): win32/app.rc win32/app.manifest
	mkdir -p $(@D)
	$(WINDRES) -Iwin32 -i $< -O coff -o $@

$(WEBVIEW2)/package.zip:
	mkdir -p $(@D)
	curl -fL --retry 2 https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3650.58/microsoft.web.webview2.1.0.3650.58.nupkg -o $@.tmp
	mv $@.tmp $@

$(WEBVIEW2)/build/native/x64/WebView2Loader.dll: $(WEBVIEW2)/package.zip
	unzip -qo $< -d $(WEBVIEW2)
	touch $@

$(GUI_RUNTIME) $(TEST_RUNTIME): $(WEBVIEW2)/build/native/x64/WebView2Loader.dll
	mkdir -p $(@D)
	cp $< $@

build/obj/native/vendor/win32_wv2.o: $(WEBUI)/include/webui.h Makefile win32/build.mk
	mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CFLAGS) -w -I$(WEBUI)/src -MMD -MP -c $(WEBUI)/src/webview/win32_wv2.cpp -o $@
