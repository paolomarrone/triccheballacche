PLUGINS = synth_mono tibia_test shape echo drums

.PHONY: all clean $(PLUGINS)
all: $(PLUGINS)

$(PLUGINS):
	$(MAKE) -C $@

clean:
	@for plugin in $(PLUGINS); do $(MAKE) -C "$$plugin" clean || exit; done
