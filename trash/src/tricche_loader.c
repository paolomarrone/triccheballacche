#include "tricche_loader.h"

#include <dlfcn.h>

static void* load_symbol(void* handle, const char* name) {
	void *s = dlsym(handle, name);
	if (!s)
		fprintf(stderr, "dlsym error reading '%s': %s\n", sym_names[i], dlerror());
	return s;
}

tibia_plugin* tricche_load_tibia(const char* path) {

	tibia_plugin *tp = malloc(sizeof(tibia_plugin));
	if (!tp) {
		fprintf(stderr, "malloc error\n");
        goto err_tibia;
	}

	tp->dl_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (!tp->dl_handle) {
        fprintf(stderr, "dlopen error: %s\n", dlerror());
        goto err_dlopen;
    }
    
	tp->new                = (tibia_new)                load_symbol(tp->dl_handle, "tibia_new");
	tp->destroy            = (tibia_destroy)            load_symbol(tp->dl_handle, "tibia_destroy");
	tp->set_sample_rate    = (tibia_set_sample_rate)    load_symbol(tp->dl_handle, "tibia_set_sample_rate");
	tp->reset              = (tibia_reset)              load_symbol(tp->dl_handle, "tibia_reset");
	tp->set_parameter      = (tibia_set_parameter)      load_symbol(tp->dl_handle, "tibia_set_parameter");
	tp->get_parameter      = (tibia_get_parameter)      load_symbol(tp->dl_handle, "tibia_get_parameter");
	tp->process            = (tibia_process)            load_symbol(tp->dl_handle, "tibia_process");
	tp->midi_msg_in        = (tibia_midi_msg_in)        load_symbol(tp->dl_handle, "tibia_midi_msg_in");
	tp->midi_msg_out       = (tibia_midi_msg_out)       load_symbol(tp->dl_handle, "tibia_midi_msg_out");
	tp->get_info           = (tibia_get_info)           load_symbol(tp->dl_handle, "tibia_get_info");
	tp->get_parameter_info = (tibia_get_parameter_info) load_symbol(tp->dl_handle, "tibia_get_info");

	// TODO: some check?

	tp->tibia_instance  = NULL;

	return tp;

err_dlopen:
	free(tp);
err_tibia:
	return NULL;
}

void tricche_unload_tibia (tibia_plugin* tp) {
	if (dlclose(tp->dl_handle))
		fprintf(stderr, "dlclose error: %s\n", dlerror());
	free(tp);
}