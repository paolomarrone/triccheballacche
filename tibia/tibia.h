#ifndef TIBIA_H
#define TIBIA_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	void *       handle;
	const char * format;
	const char * (*get_bindir)(void *handle);
	const char * (*get_datadir)(void *handle);
} tibia_callbacks;

typedef struct {
	void * handle;
	void (*lock)(void *handle);
	void (*unlock)(void *handle);
	int  (*write)(void *handle, const char *data, size_t length);
	void (*set_parameter)(void *handle, size_t index, float value);
} tibia_state_callbacks;


void*  tibia_new(             void);
void   tibia_init(            void *instance, const tibia_callbacks *cbs);
void   tibia_fini(            void *instance);
void   tibia_set_sample_rate( void *instance, float sample_rate);
size_t tibia_mem_req(         void *instance);
void   tibia_mem_set(         void *instance, void *mem);
void   tibia_reset(           void *instance);
void   tibia_set_parameter(   void *instance, size_t index, float value);
float  tibia_get_parameter(   void *instance, size_t index);
void   tibia_process(         void *instance, const float **inputs, float **outputs, size_t n_samples);
void   tibia_midi_msg_in(     void *instance, size_t index, const uint8_t * data);
int    tibia_state_save(      void *instance, const tibia_state_callbacks *cbs, float last_sample_rate);
int    tibia_state_load(const tibia_state_callbacks *cbs, float cur_sample_rate, const char *data, size_t length);

#endif