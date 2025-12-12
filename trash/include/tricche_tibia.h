#ifndef TRICCHE_TIBIA_H
#define TRICCHE_TIBIA_H

typedef void* (*tibia_new)               (void);
typedef void  (*tibia_destroy)           (void *);
typedef void  (*tibia_set_sample_rate)   (void *, float);
typedef void  (*tibia_reset)             (void *);
typedef void  (*tibia_set_parameter)     (void *, size_t, float);
typedef void  (*tibia_get_parameter)     (void *, size_t);
typedef void  (*tibia_process)           (void *, const float **, float **, size_t);
typedef void  (*tibia_midi_msg_in)       (void *, size_t index, const uint8_t *);
typedef void  (*tibia_midi_msg_out)      (void *, size_t index, const uint8_t *);
typedef void  (*tibia_get_info)          (void *, int *channels_in_n, int *channels_out_n, int *parameters_n);
typedef void  (*tibia_get_parameter_info)(void *, int index, char** name, char** shortName, char** units, char* out, char* bypass, int* steps, float* defaultValueUnmapped);

typedef struct tibia_plugin {

	void                    *dl_handle;

	tibia_new                new;
	tibia_destroy            destroy;
	tibia_set_sample_rate    set_sample_rate;
	tibia_reset              reset;
	tibia_set_parameter      set_parameter;
	tibia_get_parameter      get_parameter;
	tibia_process            process;
	tibia_midi_msg_in        midi_msg_in;
	tibia_midi_msg_out       midi_msg_out;
	tibia_get_info           get_info;
	tibia_get_parameter_info get_parameter_info

	void                    *tibia_instance;
	int                      channels_in_n;
	int                      channels_out_n;
	int                      parameters_n;
	
} tibia_plugin;


#endif
