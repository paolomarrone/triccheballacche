// Polyphonic procedural percussion. MIDI notes 0..6 select the seven sounds.
#include "tibia.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
enum { KICK, SNARE, HAT, OPEN_HAT, CRASH, TOM_HIGH, TOM_LOW, VOICES = 32 };
typedef struct { size_t age, length; int type; float strength, low; uint32_t seed; } Voice;
typedef struct { Voice voices[VOICES]; float rate, gain, seed; uint32_t rng; } Drums;
static const float tau = 6.28318530718f;
const tibia_info *tibia_get_info(void) {
	static const tibia_parameter params[] = {{"gain", "linear", 0, 1, 1, 0}, {"seed", "", 1, 16777215, 7368556, 1}};
	static const tibia_info info = {0, 1, 2, params}; return &info;
}
void *tibia_new(void) { return malloc(sizeof(Drums)); }
void tibia_init(void *p, const tibia_callbacks *c) { (void)c; *(Drums *)p = (Drums){.gain = 1, .seed = 7368556}; }
void tibia_fini(void *p) { (void)p; }
void tibia_set_sample_rate(void *p, float rate) { ((Drums *)p)->rate = rate; }
size_t tibia_mem_req(void *p) { (void)p; return 0; }
void tibia_mem_set(void *p, void *mem) { (void)p; (void)mem; }
void tibia_reset(void *p) { Drums *d = p; memset(d->voices, 0, sizeof(d->voices)); d->rng = (uint32_t)d->seed; }
void tibia_set_parameter(void *p, size_t i, float value) {
	Drums *d = p; if (i == 0) d->gain = value; else if (i == 1) { d->seed = value; d->rng = (uint32_t)value; }
}
float tibia_get_parameter(void *p, size_t i) { Drums *d = p; return i == 0 ? d->gain : d->seed; }
static uint32_t random_u32(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
void tibia_midi_msg_in(void *p, size_t bus, const uint8_t *msg) {
	(void)bus; Drums *d = p;
	if ((msg[0] & 0xf0) != 0x90 || !msg[2] || msg[1] > TOM_LOW || msg[2] > 127) return;
	const float duration[] = {.42f, .32f, .085f, .38f, 1.6f, .3f, .4f};
	Voice *v = d->voices;
	for (int i = 0; i < VOICES; ++i) {
		if (d->voices[i].age >= d->voices[i].length) { v = d->voices + i; break; }
		if (d->voices[i].age > v->age) v = d->voices + i;
	}
	*v = (Voice){.length = (size_t)(duration[msg[1]] * d->rate), .type = msg[1],
		.strength = msg[2] / 127.f, .seed = random_u32(&d->rng)};
}
void tibia_process(void *p, const float **in, float **out, size_t n) {
	(void)in; Drums *d = p;
	for (size_t i = 0; i < n; ++i) {
		float sum = 0;
		for (int k = 0; k < VOICES; ++k) {
			Voice *v = d->voices + k; if (v->age >= v->length) continue;
			int type = v->type;
			float t = (float)v->age++ / d->rate, white = (random_u32(&v->seed) >> 8) * (2.f / 16777216.f) - 1.f, x;
			v->low += .17f * (white - v->low); float high = white - v->low;
			if (type == KICK) {
				float phase = tau * (47.f * t + 3.1f * (1.f - expf(-t / .026f)));
				x = .86f * sinf(phase) * expf(-t * 12.f) + .14f * high * expf(-t * 230.f);
			} else if (type == SNARE) {
				x = .62f * high * expf(-t * 22.f) + .28f * sinf(tau * 186.f * t) * expf(-t * 27.f)
					+ .12f * sinf(tau * 337.f * t) * expf(-t * 39.f);
			} else if (type == HAT || type == OPEN_HAT || type == CRASH) {
				float metal = sinf(tau * 4177.f * t + 2.f * sinf(tau * 683.f * t))
					+ .5f * sinf(tau * 7319.f * t) + .3f * sinf(tau * 10331.f * t);
				float decay = type == HAT ? 65.f : type == OPEN_HAT ? 12.f : 3.8f;
				x = (.7f * high + .14f * metal) * expf(-t * decay);
			} else {
				float f = type == TOM_HIGH ? 172.f : 109.f;
				x = .8f * sinf(tau * (f * t + .9f * (1 - expf(-t * 35.f)))) * expf(-t * 17.f)
					+ .1f * high * expf(-t * 80.f);
			}
			sum += x * v->strength * fminf(1.f, t * 1800.f);
		}
		out[0][i] = sum * d->gain;
	}
}
