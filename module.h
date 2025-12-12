#ifndef MODULE_H
#define MODULE_H

#include <stddef.h>
#include <stdint.h>
#include "tibia/tibia.h" 

typedef struct {
	void *handle;

	void   (*init)(            void *instance, const tibia_callbacks *cbs);
	void   (*fini)(            void *instance);
	void   (*set_sample_rate)( void *instance, float sample_rate);
	size_t (*mem_req)(         void *instance);
	void   (*mem_set)(         void *instance, void *mem);
	void   (*reset)(           void *instance);
	void   (*set_parameter)(   void *instance, size_t index, float value);
	float  (*get_parameter)(   void *instance, size_t index);
	void   (*process)(         void *instance, const float **inputs, float **outputs, size_t n_samples);
	void   (*midi_msg_in)(     void *instance, size_t index, const uint8_t *data);
//	int    (*state_save)(      void *instance, const tibia_state_callbacks *cbs, float last_sample_rate);
//	int    (*state_load)(const tibia_state_callbacks *cbs, float cur_sample_rate, const char *data, size_t length);

} TibiaModule;

#endif