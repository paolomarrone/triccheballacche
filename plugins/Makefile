PLUGINS = synth_mono fx_svf tibia_test shape echo drums

.PHONY: all clean $(PLUGINS) piano
all: $(PLUGINS)

$(PLUGINS) piano:
	$(MAKE) -C $@

clean:
	@for plugin in $(PLUGINS) piano; do $(MAKE) -C "$$plugin" clean || exit; done
