// Fixed-bank PCM piano. Samples are immutable; rendering and MIDI never allocate.
#ifndef PIANO_H
#define PIANO_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { PIANO_VOICES = 192 };
enum { PIANO_DELAY, PIANO_ATTACK, PIANO_HOLD, PIANO_DECAY, PIANO_SUSTAIN, PIANO_RELEASE, PIANO_DONE };
typedef struct {
	float delay, attack, hold, decay, sustain, release, key_hold, key_decay;
} PianoEnvelope;
typedef struct {
	uint32_t start, end, loop_start, loop_end, rate;
	int key_low, key_high, velocity_low, velocity_high, root, loop;
	float tune, scale, gain, pan, cutoff, resonance, filter_envelope;
	PianoEnvelope amplitude, modulation;
} PianoRegion;
#include "piano_data.h"

typedef struct {
	uint64_t remaining, duration[4];
	float level, change, sustain, release;
	int stage, exponential;
} PianoEnvelopeState;
typedef struct {
	const PianoRegion *region;
	PianoEnvelopeState amplitude, modulation;
	double position, step, base_step;
	double a0, a1, a2, b1, b2, z1, z2;
	float gain, left, right, cutoff;
	uint64_t serial;
	unsigned filter_clock;
	int key, channel, layer, held, filtering;
} PianoVoice;
typedef struct {
	PianoVoice voices[PIANO_VOICES];
	uint64_t serial;
	float rate, gain, detune, blend;
	unsigned char sustain[16];
} Piano;

static uint64_t piano_time(float cents, float rate) {
	// Treat the SoundFont default (<1 ms) as an instantaneous stage.
	return cents <= -11950 ? 0 : (uint64_t)(exp2(fmin((double)cents, 8000) / 1200) * rate);
}

static void piano_envelope_next(PianoEnvelopeState *e) {
	while (++e->stage < PIANO_SUSTAIN) {
		e->remaining = e->duration[e->stage];
		if (e->stage == PIANO_DELAY)
			e->level = e->change = 0;
		else if (e->stage == PIANO_ATTACK) {
			e->level = 0;
			e->change = e->remaining ? 1.f / e->remaining : 0;
		} else if (e->stage == PIANO_HOLD) {
			e->level = 1;
			e->change = 0;
		} else {
			e->level = 1;
			if (e->remaining) {
				// Decay is the time to silence, shortened when sustain is above it.
				e->change = e->exponential ? expf(-9.210340372f / e->remaining) : -1.f / e->remaining;
				if (e->sustain > 0)
					e->remaining = (uint64_t)(e->remaining *
					    (e->exponential ? -logf(e->sustain) / 9.210340372f : 1.f - e->sustain));
			}
		}
		if (e->remaining)
			return;
	}
	if (e->stage == PIANO_SUSTAIN)
		e->level = e->sustain;
	else {
		e->level = 0;
		e->stage = PIANO_DONE;
	}
}

static void piano_envelope_init(
    PianoEnvelopeState *e, const PianoEnvelope *p, int key, int velocity, float rate, int amplitude) {
	*e = (PianoEnvelopeState){.stage = -1, .exponential = amplitude};
	e->duration[0] = piano_time(p->delay, rate);
	e->duration[1] = piano_time(p->attack, rate);
	if (!amplitude)
		e->duration[1] = e->duration[1] * (145 - velocity) / 144;
	e->duration[2] = piano_time(p->hold + p->key_hold * (60 - key), rate);
	e->duration[3] = piano_time(p->decay + p->key_decay * (60 - key), rate);
	e->sustain = amplitude ? powf(10, -fmaxf(0, p->sustain) / 200) : 1 - fminf(1, fmaxf(0, p->sustain) / 1000);
	e->release = (float)piano_time(p->release, rate);
	if (e->release < 1)
		e->release = fmaxf(1, .005f * rate);
	piano_envelope_next(e);
}

static void piano_envelope_release(PianoEnvelopeState *e, uint64_t samples) {
	if (e->stage >= PIANO_RELEASE)
		return;
	e->stage = PIANO_RELEASE;
	e->remaining = samples ? samples : (uint64_t)e->release;
	e->change = e->exponential ? expf(-9.210340372f / e->remaining) : -e->level / e->remaining;
}

static float piano_envelope_tick(PianoEnvelopeState *e) {
	float value = e->level;
	if (e->stage < PIANO_SUSTAIN || e->stage == PIANO_RELEASE) {
		if (e->exponential && e->stage >= PIANO_DECAY)
			e->level *= e->change;
		else
			e->level += e->change;
		if (!--e->remaining)
			piano_envelope_next(e);
	}
	return value;
}

static void piano_release(PianoVoice *v, uint64_t samples) {
	piano_envelope_release(&v->amplitude, samples);
	piano_envelope_release(&v->modulation, samples);
	v->held = 0;
}

static void piano_reset(Piano *p) {
	memset(p->voices, 0, sizeof(p->voices));
	memset(p->sustain, 0, sizeof(p->sustain));
	p->serial = 0;
}

static void piano_filter(PianoVoice *v, float rate) {
	const PianoRegion *r = v->region;
	float cents = r->cutoff + r->filter_envelope * v->modulation.level;
	if (cents == v->cutoff)
		return;
	v->cutoff = cents;
	double frequency = 8.176 * exp2((double)cents / 1200) / rate;
	v->filtering = frequency < .499;
	if (!v->filtering)
		return;
	// Bilinear transform of a two-pole low-pass; Q=1 at zero resonance.
	double k = tan(3.141592653589793 * frequency), k2 = k * k;
	double damping = pow(10, -r->resonance / 200.0), denominator = 1 + damping * k + k2;
	v->a0 = v->a2 = k2 / denominator;
	v->a1 = 2 * v->a0;
	v->b1 = 2 * (k2 - 1) / denominator;
	v->b2 = (1 - damping * k + k2) / denominator;
}

static int piano_matches(const PianoRegion *r, int key, int velocity) {
	return key >= r->key_low && key <= r->key_high && velocity >= r->velocity_low && velocity <= r->velocity_high;
}

static void piano_note_on(Piano *p, int channel, int key, int velocity) {
	unsigned needed = 0, available = 0;
	for (size_t i = 0; i < sizeof(piano_regions) / sizeof(*piano_regions); ++i)
		if (piano_matches(piano_regions + i, key, velocity))
			needed += 2;
	if (!needed || needed > PIANO_VOICES)
		return;
	for (int i = 0; i < PIANO_VOICES; ++i) {
		PianoVoice *v = p->voices + i;
		if (!v->region)
			++available;
		else if (v->channel == channel && v->key == key)
			piano_release(v, (uint64_t)fmaxf(1, p->rate * .005f));
	}
	while (available < needed) {
		// Steal a whole note, including both stereo samples and detuned layers.
		// Prefer released notes, then the oldest. Never drop just one stereo side.
		PianoVoice *oldest = NULL;
		for (int i = 0; i < PIANO_VOICES; ++i) {
			PianoVoice *v = p->voices + i;
			if (!v->region)
				continue;
			int released = v->amplitude.stage >= PIANO_RELEASE;
			int old_released = oldest && oldest->amplitude.stage >= PIANO_RELEASE;
			if (!oldest || released > old_released || (released == old_released && v->serial < oldest->serial))
				oldest = v;
		}
		uint64_t serial = oldest->serial;
		for (int i = 0; i < PIANO_VOICES; ++i)
			if (p->voices[i].region && p->voices[i].serial == serial) {
				p->voices[i].region = NULL;
				++available;
			}
	}
	++p->serial;
	int slot = 0;
	for (size_t i = 0; i < sizeof(piano_regions) / sizeof(*piano_regions); ++i) {
		const PianoRegion *r = piano_regions + i;
		if (!piano_matches(r, key, velocity))
			continue;
		for (int layer = 0; layer < 2; ++layer) {
			while (p->voices[slot].region)
				++slot;
			PianoVoice *v = p->voices + slot++;
			*v = (PianoVoice){.region = r,
			    .position = r->start,
			    .serial = p->serial,
			    .key = key,
			    .channel = channel,
			    .layer = layer,
			    .held = 1,
			    .cutoff = -1};
			v->base_step = (double)r->rate / p->rate * exp2(((key - r->root) * r->scale + r->tune) / 1200.0);
			v->step = v->base_step * (layer ? exp2(p->detune / 1200.0) : 1);
			v->gain = r->gain * (velocity / 127.f) * .398107171f; // -8 dB headroom.
			float pan = fmaxf(-.5f, fminf(.5f, r->pan + (layer ? .05f : -.05f)));
			v->left = sqrtf(.5f - pan);
			v->right = sqrtf(.5f + pan);
			piano_envelope_init(&v->amplitude, &r->amplitude, key, velocity, p->rate, 1);
			piano_envelope_init(&v->modulation, &r->modulation, key, velocity, p->rate, 0);
		}
	}
}

static void piano_midi(Piano *p, const uint8_t *data) {
	int kind = data[0] & 0xf0, channel = data[0] & 15;
	if (data[1] > 127 || data[2] > 127)
		return;
	if (kind == 0x90 && data[2]) {
		piano_note_on(p, channel, data[1], data[2]);
		return;
	}
	if (kind == 0xb0 && data[1] == 64)
		p->sustain[channel] = data[2] >= 64;
	for (int i = 0; i < PIANO_VOICES; ++i) {
		PianoVoice *v = p->voices + i;
		if (!v->region || v->channel != channel)
			continue;
		if (kind == 0xb0 && data[1] == 120) {
			v->region = NULL;
			continue;
		}
		int off = ((kind == 0x80 || kind == 0x90) && v->key == data[1]) || (kind == 0xb0 && data[1] == 123);
		if (off)
			v->held = 0;
		if ((off || (kind == 0xb0 && data[1] == 64)) && !v->held && !p->sustain[channel])
			piano_release(v, 0);
	}
}

static void piano_process(Piano *p, float *left, float *right, size_t count) {
	memset(left, 0, count * sizeof(float));
	memset(right, 0, count * sizeof(float));
	for (int i = 0; i < PIANO_VOICES; ++i) {
		PianoVoice *v = p->voices + i;
		const PianoRegion *r = v->region;
		if (!r)
			continue;
		float gain = v->gain * p->gain * (v->layer ? p->blend : 1);
		for (size_t j = 0; j < count; ++j) {
			int loop = r->loop == 1 || (r->loop == 3 && v->amplitude.stage < PIANO_RELEASE);
			if (loop && v->position >= r->loop_end)
				v->position = r->loop_start + fmod(v->position - r->loop_start, r->loop_end - r->loop_start);
			if (v->amplitude.stage == PIANO_DONE || v->position >= r->end) {
				v->region = NULL;
				break;
			}
			uint32_t index = (uint32_t)v->position, next = index + 1;
			if (loop && next == r->loop_end)
				next = r->loop_start;
			if (next >= r->end)
				next = index;
			double fraction = v->position - index;
			double value = (piano_samples[index] + fraction * (piano_samples[next] - piano_samples[index])) / 32768.0;
			if (!v->filter_clock)
				piano_filter(v, p->rate);
			v->filter_clock = (v->filter_clock + 1) % 32;
			if (v->filtering) {
				double filtered = v->a0 * value + v->z1;
				v->z1 = v->a1 * value - v->b1 * filtered + v->z2;
				v->z2 = v->a2 * value - v->b2 * filtered;
				value = filtered;
			}
			float sample = (float)value * gain * piano_envelope_tick(&v->amplitude);
			piano_envelope_tick(&v->modulation);
			left[j] += sample * v->left;
			right[j] += sample * v->right;
			v->position += v->step;
		}
	}
}
#endif
