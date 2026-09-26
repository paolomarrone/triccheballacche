// Exercise the embedded bank through the same callbacks as the Perone wrapper.
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api.h"
#include "plugin.h"

static void midi(plugin *p, int type, int key, int value) {
	uint8_t message[] = {type, key, value};
	plugin_midi_msg_in(p, 0, message);
}

static unsigned active(const plugin *p) {
	unsigned count = 0;
	for (int i = 0; i < PIANO_VOICES; ++i)
		count += p->voices[i].region != NULL;
	return count;
}

static double render(plugin *p, size_t count) {
	float left[1026], right[1026];
	assert(count <= 1025);
	left[count] = right[count] = 12345;
	float *out[] = {left, right};
	plugin_process(p, NULL, out, count);
	assert(left[count] == 12345 && right[count] == 12345);
	double energy = 0;
	for (size_t i = 0; i < count; ++i) {
		assert(isfinite(left[i]) && isfinite(right[i]));
		energy += left[i] * left[i] + right[i] * right[i];
	}
	return energy;
}

static void advance(plugin *p, size_t count) {
	while (count) {
		size_t n = count < 1025 ? count : 1025;
		render(p, n);
		count -= n;
	}
}

static void bank(void) {
	assert(sizeof(piano_regions) / sizeof(*piano_regions) == 360);
	for (int key = 0; key < 128; ++key)
		for (int velocity = 1; velocity < 128; ++velocity) {
			unsigned matches = 0;
			for (size_t i = 0; i < sizeof(piano_regions) / sizeof(*piano_regions); ++i) {
				const PianoRegion *r = piano_regions + i;
				if (!piano_matches(r, key, velocity))
					continue;
				++matches;
				assert(r->start < r->loop_start && r->loop_start < r->loop_end && r->loop_end <= r->end);
				assert(r->end <= sizeof(piano_samples) / sizeof(*piano_samples));
			}
			assert(matches == (key <= 108 ? 2u : 0u));
		}
	puts("OK: complete key/velocity mapping, stereo regions and sample/loop bounds");
}

static void lifecycle(unsigned rate) {
	plugin p;
	// No filesystem or host callbacks are required, on either platform.
	assert(!plugin_init(&p, NULL));
	assert(!plugin_mem_req(&p));
	plugin_mem_set(&p, NULL);
	plugin_set_sample_rate(&p, rate);
	assert(!render(&p, 1025));
	midi(&p, 0x90, 60, 45);
	double soft = render(&p, 1025);
	plugin_reset(&p);
	midi(&p, 0x90, 60, 112);
	double loud = render(&p, 1025);
	assert(soft > 0 && loud > soft * 2);
	const int chord[] = {36, 48, 55, 59, 64, 67, 72, 76};
	for (size_t i = 0; i < sizeof(chord) / sizeof(*chord); ++i)
		midi(&p, 0x90, chord[i], 92);
	assert(active(&p) == 36);
	assert(render(&p, 1) > 0 && render(&p, 127) > 0 && render(&p, 256) > 0);
	midi(&p, 0xb0, 64, 127);
	midi(&p, 0x90, 60, 0);
	for (size_t i = 0; i < sizeof(chord) / sizeof(*chord); ++i)
		midi(&p, 0x80, chord[i], 0);
	advance(&p, rate);
	assert(active(&p) == 36);
	midi(&p, 0xb0, 64, 0);
	advance(&p, rate * 4);
	assert(!active(&p) && !render(&p, 256));
	plugin_set_parameter(&p, plugin_parameter_detune, 19);
	plugin_set_parameter(&p, plugin_parameter_blend, .2f);
	assert(plugin_get_parameter(&p, plugin_parameter_detune) == 19);
	midi(&p, 0x90, 69, 100);
	assert(render(&p, 1025) > 0);
	plugin_reset(&p);
	assert(!render(&p, 1025));
	midi(&p, 0x90, 69, 100);
	assert(render(&p, 1025) > 0);
	plugin_fini(&p);
	plugin_fini(&p);
	printf("OK: %u Hz polyphony, velocity, pedal, release and reset\n", rate);
}

static void render_split(plugin *p, float *left, float *right, size_t count, size_t block) {
	for (size_t i = 0; i < count;) {
		size_t n = count - i < block ? count - i : block;
		float *out[] = {left + i, right + i};
		plugin_process(p, NULL, out, n);
		i += n;
	}
}

static void blocks(void) {
	const size_t count = 16000;
	float *a = malloc(count * 4 * sizeof(float));
	assert(a);
	plugin p, q;
	assert(!plugin_init(&p, NULL));
	plugin_set_sample_rate(&p, 32000);
	midi(&p, 0x90, 96, 100);
	midi(&p, 0x90, 60, 80);
	advance(&p, 40000); // Cross the high note's sample loop before comparing.
	midi(&p, 0x80, 60, 0);
	q = p;
	const size_t sizes[] = {1, 17, 127, 256, 1025};
	for (size_t b = 0; b < sizeof(sizes) / sizeof(*sizes); ++b) {
		plugin x = p, y = q;
		render_split(&x, a, a + count, count, count);
		render_split(&y, a + 2 * count, a + 3 * count, count, sizes[b]);
		assert(!memcmp(a, a + 2 * count, count * 2 * sizeof(float)));
		assert(memcmp(a, a + count, count * sizeof(float))); // Real stereo samples.
	}
	q = p;
	plugin_set_parameter(&q, plugin_parameter_detune, 25);
	plugin_set_parameter(&q, plugin_parameter_blend, .7f);
	render_split(&p, a, a + count, count, 127);
	render_split(&q, a + 2 * count, a + 3 * count, count, 127);
	assert(memcmp(a, a + 2 * count, count * 2 * sizeof(float)));
	plugin_set_parameter(&q, plugin_parameter_gain, 0);
	assert(!render(&q, 1025));
	free(a);
	puts("OK: exact block invariance across loops/releases, stereo and live controls");
}

static void messages(void) {
	plugin p;
	assert(!plugin_init(&p, NULL));
	midi(&p, 0x90, 60, 100);
	advance(&p, 1000);
	midi(&p, 0x90, 60, 100);
	assert(active(&p) == 8);
	advance(&p, 1000);
	assert(active(&p) == 4);
	midi(&p, 0x91, 64, 100);
	midi(&p, 0xb0, 123, 0);
	advance(&p, 100000);
	assert(active(&p) == 4); // CC123 on channel 0 leaves channel 1 alone.
	midi(&p, 0xb1, 64, 127);
	midi(&p, 0xb1, 123, 0);
	advance(&p, 100000);
	assert(active(&p) == 4);
	midi(&p, 0xb1, 120, 0);
	assert(!active(&p) && !render(&p, 256));
	plugin_reset(&p);
	for (int i = 0; i < 1000; ++i) {
		midi(&p, 0x90, 21 + i % 88, 1 + i % 127);
		assert(active(&p) <= PIANO_VOICES && active(&p) % 4 == 0);
		render(&p, 17);
	}
	midi(&p, 0xb0, 123, 0);
	advance(&p, 200000);
	assert(!active(&p));
	for (int i = 0; i < 128; ++i) {
		plugin_reset(&p);
		midi(&p, 0x90, i, 127);
		render(&p, 1025);
	}
	plugin_set_parameter(&p, plugin_parameter_gain, NAN);
	assert(p.gain == .6f);
	plugin_set_parameter(&p, plugin_parameter_gain, 100);
	plugin_set_parameter(&p, plugin_parameter_detune, -1);
	plugin_set_parameter(&p, plugin_parameter_blend, INFINITY);
	assert(p.gain == 2 && p.detune == 0 && p.blend == .32f);
	const unsigned rates[] = {1, 8000, 192000, 384000};
	for (size_t i = 0; i < sizeof(rates) / sizeof(*rates); ++i) {
		plugin_set_sample_rate(&p, rates[i]);
		midi(&p, 0x90, 108, 127);
		advance(&p, 4096);
		midi(&p, 0x80, 108, 0);
		advance(&p, 4096);
	}
	puts("OK: retriggers, MIDI channels, bounded whole-note stealing, controllers and extreme rates");
}

// A short deterministic performance shared with the standalone Wasm check.
static void reference(const char *path) {
	FILE *file = fopen(path, "wb");
	assert(file);
	plugin p;
	assert(!plugin_init(&p, NULL));
	plugin_set_sample_rate(&p, 32000);
	for (int block = 0; block < 256; ++block) {
		switch (block) {
		case 0:
			midi(&p, 0x90, 96, 100);
			midi(&p, 0x90, 48, 72);
			break;
		case 16:
			midi(&p, 0x90, 60, 112);
			break;
		case 32:
			midi(&p, 0xb0, 64, 127);
			break;
		case 48:
			midi(&p, 0x80, 96, 0);
			midi(&p, 0x80, 48, 0);
			midi(&p, 0x80, 60, 0);
			break;
		case 64:
			plugin_set_parameter(&p, plugin_parameter_blend, .7f);
			plugin_set_parameter(&p, plugin_parameter_detune, 23);
			break;
		case 96:
			midi(&p, 0xb0, 64, 0);
			break;
		case 192:
			midi(&p, 0x90, 36, 120);
			midi(&p, 0x90, 64, 100);
			break;
		case 208:
			midi(&p, 0x90, 64, 80);
			break;
		case 224:
			midi(&p, 0xb0, 123, 0);
			break;
		}
		float left[512], right[512], audio[1024], *out[] = {left, right};
		plugin_process(&p, NULL, out, 512);
		for (int i = 0; i < 512; ++i) {
			audio[2 * i] = left[i];
			audio[2 * i + 1] = right[i];
		}
		assert(fwrite(audio, sizeof(float), 1024, file) == 1024);
	}
	assert(!fclose(file));
}

int main(int argc, char **argv) {
	if (argc == 3 && !strcmp(argv[1], "--write-pcm")) {
		reference(argv[2]);
		return 0;
	}
	assert(argc == 1);
	bank();
	lifecycle(32000);
	lifecycle(44100);
	lifecycle(48000);
	blocks();
	messages();
	return 0;
}
