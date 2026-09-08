include ../plugin.mk

BRICKWORKS ?= .deps/brickworks
CPPFLAGS += -I$(BRICKWORKS)/include
build/plugin.so: parameters.h $(BRICKWORKS)/.patched

$(BRICKWORKS)/include/bw_common.h:
	git clone --depth 1 --branch v1.2.0 https://github.com/Orastron/brickworks.git $(BRICKWORKS)

$(BRICKWORKS)/.patched: $(BRICKWORKS)/include/bw_common.h Makefile
	# The sqrt approximation needs unsigned shifts (also checked with UBSan).
	sed -i 's/0x200000e0 <</0x200000e0u <</; s/0x100000f0 <</0x100000f0u <</' $(BRICKWORKS)/include/bw_math.h
	touch $@
