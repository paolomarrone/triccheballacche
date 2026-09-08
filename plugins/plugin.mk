# Shared build rules for the example plugins, independent of the host Makefile.
CC = cc
CFLAGS ?= -O2 -Wall -Wextra
TIBIA ?= ../../tibia
.DEFAULT_GOAL := all

.PHONY: all clean
all: build/plugin.so

build/plugin.so: plugin.c $(TIBIA)/tibia.h Makefile ../plugin.mk
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -I$(TIBIA) -shared $< $(LDFLAGS) -lm -o $@

clean:
	rm -f build/plugin.so
