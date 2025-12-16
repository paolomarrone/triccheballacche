#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "loader.h"

#define SAMPLE_RATE 44100
#define CHANNELS 1
#define MAX_BUFFER_FRAMES 4096 
#define MAX_EVENTS 1024

// --- 1. Event Definitions ---

typedef enum {
	EVENT_PARAM,
	EVENT_MIDI
} EventType;

typedef struct {
	size_t timestamp; // Absolute sample position
	EventType type;
	union {
		struct {
			size_t index;
			float value;
		} param;
		struct {
			uint8_t data[3]; // Standard 3-byte MIDI message
		} midi;
	};
} TibiaEvent;

typedef struct {
	TibiaEvent events[MAX_EVENTS];
	size_t count;
	size_t current_index; // Next event to fire
} TibiaSequence;

// --- 2. Engine Context ---

typedef struct {
	TibiaModule *module;
	void *plugin_instance;
	void *plugin_memory;

	// Timeline state
	size_t global_time; // Total samples elapsed since start
	TibiaSequence seq;

	// Buffers
	float *output_planar_storage[CHANNELS];
	float *output_planar_ptrs[CHANNELS];
} AudioEngine;

// --- 3. Audio Callback (The Scheduler) ---

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
	AudioEngine *engine = (AudioEngine*)pDevice->pUserData;
	float *maOut = (float*)pOutput;

	// Safety checks
	if (!engine->plugin_instance || frameCount > MAX_BUFFER_FRAMES) {
		memset(maOut, 0, frameCount * CHANNELS * sizeof(float));
		return;
	}

	size_t frames_remaining = frameCount;
	size_t buffer_offset = 0;

	// --- Sub-buffer Processing Loop ---
	while (frames_remaining > 0) {
		
		// 1. Calculate how many frames we can process before the next event
		size_t next_event_time = (size_t)-1; // Infinity
		
		if (engine->seq.current_index < engine->seq.count) {
			next_event_time = engine->seq.events[engine->seq.current_index].timestamp;
		}

		size_t current_abs_time = engine->global_time + buffer_offset;
		size_t frames_until_event = 0;

		if (next_event_time > current_abs_time) {
			frames_until_event = next_event_time - current_abs_time;
		} else {
			// Event is NOW (or in the past due to jitter), process immediately
			frames_until_event = 0;
		}

		// Limit to remaining buffer size
		size_t process_count = frames_remaining;
		if (frames_until_event < frames_remaining) {
			process_count = frames_until_event;
		}

		// 2. Render Audio (if there is a gap before the event)
		if (process_count > 0) {
			// We need temporary pointers offset into the buffer
			float *offset_ptrs[CHANNELS];
			for (int c = 0; c < CHANNELS; c++) {
				offset_ptrs[c] = engine->output_planar_storage[c] + buffer_offset;
			}

			engine->module->process(
				engine->plugin_instance, 
				NULL, 
				offset_ptrs, 
				process_count
			);

			frames_remaining -= process_count;
			buffer_offset += process_count;
		}

		// 3. Handle Event (if we hit the split point)
		// We check frames_remaining > 0 because if we just finished the whole buffer, 
		// the event shouldn't fire until the START of the next callback.
		if (frames_remaining > 0 && engine->seq.current_index < engine->seq.count) {
			 // Double check timing to be precise
			 if ((engine->global_time + buffer_offset) >= next_event_time) {
				 
				 TibiaEvent *evt = &engine->seq.events[engine->seq.current_index];
				 
				 if (evt->type == EVENT_PARAM) {
					 printf("Param: %zu -> %f\n", evt->param.index, evt->param.value);
					 engine->module->set_parameter(engine->plugin_instance, evt->param.index, evt->param.value);
				 } 
				 else if (evt->type == EVENT_MIDI) {
					 printf("MIDI: %02x\n", evt->midi.data[0]);
					 if (engine->module->midi_msg_in) {
						engine->module->midi_msg_in(engine->plugin_instance, 0, evt->midi.data);
					 }
				 }

				 engine->seq.current_index++;
				 
				 // Do not advance buffer_offset here, we just changed state. 
				 // The loop continues to render the rest of the audio with new state.
			 }
		}
	}

	// Advance Global Time
	engine->global_time += frameCount;

	// --- 4. Interleave Output ---
	for (ma_uint32 i = 0; i < frameCount; i++) {
		for (int c = 0; c < CHANNELS; c++) {
			maOut[i * CHANNELS + c] = engine->output_planar_storage[c][i];
		}
	}
}

// Helper to add events
void add_param_event(AudioEngine *eng, float secs, size_t idx, float val) {
	if (eng->seq.count >= MAX_EVENTS) return;
	size_t timestamp = (size_t) (secs * SAMPLE_RATE);
	TibiaEvent *e = &eng->seq.events[eng->seq.count++];
	e->timestamp = timestamp;
	e->type = EVENT_PARAM;
	e->param.index = idx;
	e->param.value = val;
}

void add_midi_event(AudioEngine *eng, float secs, uint8_t status, uint8_t d1, uint8_t d2) {
	if (eng->seq.count >= MAX_EVENTS) return;
	size_t timestamp = (size_t) (secs * SAMPLE_RATE);
	TibiaEvent *e = &eng->seq.events[eng->seq.count++];
	e->timestamp = timestamp;
	e->type = EVENT_MIDI;
	e->midi.data[0] = status;
	e->midi.data[1] = d1;
	e->midi.data[2] = d2;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Usage: %s <path_to_plugin.so>\n", argv[0]);
		return 1;
	}

	const char *plugin_path = argv[1];
	AudioEngine engine = {0};

	// --- Initialize ---
	engine.module = tibia_loader_load(plugin_path);
	if (!engine.module) return 1;

	engine.plugin_instance = engine.module->new();
	engine.module->init(engine.plugin_instance, NULL);

	size_t req_size = engine.module->mem_req(engine.plugin_instance);
	if (req_size > 0) {
		engine.plugin_memory = malloc(req_size);
		engine.module->mem_set(engine.plugin_instance, engine.plugin_memory);
	}
	engine.module->set_sample_rate(engine.plugin_instance, (float)SAMPLE_RATE);

	// --- POPULATE SEQUENCE ---
	// Note: We use the parameters from your previous example (0=Vol, 37=Gate)
	// to ensure you hear sound, but the mechanism is generic.
	
	printf("Populating sequence...\n");

	// T=0: Set Volume 50
	add_param_event(&engine, 0.f, 0, 100.0f);
//	add_param_event(&engine, 0.f, 37, 1.0f);

    add_midi_event(&engine, 3.f, 0x90, 60, 100); // Note ON, Vel 100
    add_midi_event(&engine, 4.5f, 0x80, 60, 0);   // Note OFF


    add_midi_event(&engine, 5.f, 0x90, 64, 100); // Note ON
    add_midi_event(&engine, 6.f, 0x90, 64, 0);   // Note ON (Vel 0) -> acts as OFF

    add_midi_event(&engine, 7.f, 0x90, 67, 100);

    add_midi_event(&engine, 8.f,  0xE0, 0x00, 0x50); // Slight Up
    add_midi_event(&engine, 9.f, 0xE0, 0x00, 0x60); // More Up
    add_midi_event(&engine, 10.f, 0xE0, 0x7F, 0x7F); // MAX Up (approx +1 semitone or octave depending on synth)

    add_midi_event(&engine, 11.f, 0xE0, 0x00, 0x40); 
    
    add_midi_event(&engine, 12.f, 0x80, 67, 0);

//	add_param_event(&engine, 12.f, 37, 0.0f);


	// --- Audio Setup ---
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
		return -1;
	}

	printf("Starting Sequence... Press ENTER to quit.\n");
	ma_device_start(&device);

	getchar();

	ma_device_uninit(&device);
	
	// Cleanup
	for (int c = 0; c < CHANNELS; c++) free(engine.output_planar_storage[c]);
	engine.module->fini(engine.plugin_instance);
	if (engine.plugin_memory) free(engine.plugin_memory);
	free(engine.plugin_instance);
	tibia_loader_unload(engine.module);

	return 0;
}