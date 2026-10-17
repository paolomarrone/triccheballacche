# Platform selection only; compilation rules stay in the root Makefile.
WINDOW_SYSTEM = x11
override CPPFLAGS += -Ix11 -Itest/posix -Itest/x11
LDLIBS = -lm -pthread
WINDOW_LIBS = -lX11
MIDI_LIBS = -lasound
GUI_LOOP = posix/gui_loop.o
TEST_UI_OBJECTS = test/x11/ui.o
ifeq ($(TARGET_OS),Linux)
GUI_LOOP = gtk/gui_loop.o
endif
ifeq ($(TARGET_OS),Darwin)
MIDI_LIBS = -framework CoreMIDI -framework CoreAudio -framework CoreFoundation
else
LDLIBS += -ldl
endif
ifeq ($(shell uname -o 2>/dev/null),Android)
LDLIBS += -landroid-spawn
endif
