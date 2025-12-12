#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "loader.h"

#define SAMPLE_RATE 44100
#define CHANNELS 1
#define MAX_BUFFER_FRAMES 4096 

typedef struct {
	TibiaModule *module;
	void *plugin_instance;
	void *plugin_memory;

	// miniaudio is Interleaved (LRLR), tibia is Planar (LL, RR)
	float *output_planar_storage[CHANNELS];
	float *output_planar_ptrs[CHANNELS];
} AudioEngine;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
	AudioEngine *engine = (AudioEngine*)pDevice->pUserData;
	float *maOut = (float*)pOutput;

	if (!engine->plugin_instance || frameCount > MAX_BUFFER_FRAMES) {
		memset(maOut, 0, frameCount * CHANNELS * sizeof(float));
		return;
	}

	engine->module->process(
		engine->plugin_instance, 
		NULL, 
		engine->output_planar_ptrs, 
		frameCount
	);

	// tibia (LLLL, RRRR) -> miniaudio (LRLRLRLR)
	for (ma_uint32 i = 0; i < frameCount; i++) {
		for (int c = 0; c < CHANNELS; c++) {
			maOut[i * CHANNELS + c] = engine->output_planar_ptrs[c][i];
		}
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Usage: %s <path_to_plugin.so>\n", argv[0]);
		return 1;
	}

	const char *plugin_path = argv[1];
	AudioEngine engine = {0};

	printf("Loading plugin: %s\n", plugin_path);
	engine.module = tibia_loader_load(plugin_path);
	if (!engine.module) {
		fprintf(stderr, "Failed to load module.\n");
		return 1;
	}

	engine.plugin_instance = engine.module->new();
	if (!engine.plugin_instance) {
		fprintf(stderr, "Failed to create plugin instance.\n");
		return 1;
	}

	engine.module->init(engine.plugin_instance, NULL);

	size_t req_size = engine.module->mem_req(engine.plugin_instance);
	engine.plugin_memory = NULL;
	if (req_size > 0) {
		engine.plugin_memory = malloc(req_size);
		engine.module->mem_set(engine.plugin_instance, engine.plugin_memory);
		printf("Allocated %zu bytes of extra memory for plugin.\n", req_size);
	}

	engine.module->set_sample_rate(engine.plugin_instance, (float)SAMPLE_RATE);

	// Test
	engine.module->set_parameter(engine.plugin_instance, 0, 50.f);
	engine.module->set_parameter(engine.plugin_instance, 37, 1.0f);

	for (int c = 0; c < CHANNELS; c++) {
		engine.output_planar_storage[c] = calloc(MAX_BUFFER_FRAMES, sizeof(float));
		engine.output_planar_ptrs[c] = engine.output_planar_storage[c];
	}

	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format   = ma_format_f32;
	config.playback.channels = CHANNELS;
	config.sampleRate        = SAMPLE_RATE;
	config.dataCallback      = data_callback;
	config.pUserData         = &engine;

	ma_device device;
	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		fprintf(stderr, "Failed to initialize playback device.\n");
		return -1;
	}

	printf("Starting Audio... Press ENTER to quit.\n");
	
	if (ma_device_start(&device) != MA_SUCCESS) {
		fprintf(stderr, "Failed to start playback device.\n");
		ma_device_uninit(&device);
		return -1;
	}

	getchar();

	printf("Shutting down...\n");
	ma_device_uninit(&device);

	for (int c = 0; c < CHANNELS; c++) {
		free(engine.output_planar_storage[c]);
	}

	engine.module->fini(engine.plugin_instance);

    free(engine.plugin_memory);
	free(engine.plugin_instance);

	tibia_loader_unload(engine.module);

	return 0;
}