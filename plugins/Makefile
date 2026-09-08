PLUGINS = synth_mono fx_svf tibia_test shape echo drums

.PHONY: all clean $(PLUGINS)
all: $(PLUGINS)

$(PLUGINS):
	$(MAKE) -C $@

clean:
	@for plugin in $(PLUGINS); do $(MAKE) -C "$$plugin" clean || exit; done
