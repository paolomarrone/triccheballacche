/*
 * Tibia
 *
 * Copyright (C) 2023-2025 Orastron Srl unipersonale
 *
 * Tibia is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Tibia is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Tibia.  If not, see <http://www.gnu.org/licenses/>.
 *
 * File author: Stefano D'Angelo
 * Modified by: Poalo Marrone
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "tibia.h"

const tibia_info *tibia_get_info(void) {
	static const tibia_parameter params[] = {
		{"gain", "dB", -60, 12, 0, 0}, {"delay", "ms", 0, 1000, 0, 0},
		{"cutoff", "Hz", 20, 20000, 1000, 0}, {"bypass", "", 0, 1, 0, 1}
	};
	static const tibia_info info = {1, 1, 4, params};
	return &info;
}

typedef struct {
	float	sample_rate;
	size_t	delay_line_length;

	float	gain;
	float	delay;
	float	cutoff;
	char	bypass;

	float *	delay_line;
	size_t	delay_line_cur;
	float	z1;
	float	cutoff_k;
	float	yz1;
} test;


enum {
	parameter_gain,
	parameter_delay,
	parameter_cutoff,
	parameter_bypass,
};

void* tibia_new(void) {
	return malloc(sizeof(test));
}

void tibia_init(void *vinstance, const tibia_callbacks *cbs) {
	*(test *)vinstance = (test){.cutoff = 1000.f};
	(void)cbs;
}

void tibia_fini(void *vinstance) {
	(void)vinstance;
}

void tibia_set_sample_rate(void *vinstance, float sample_rate) {
	test *instance = (test*) vinstance;

	instance->sample_rate = sample_rate;
	//safe approx instance->delay_line_length = ceilf(sample_rate) + 1;
	instance->delay_line_length = (size_t)(sample_rate + 1.f) + 1;
}

size_t tibia_mem_req(void *vinstance) {
	test *instance = (test*) vinstance;

	return instance->delay_line_length * sizeof(float);
}

void tibia_mem_set(void *vinstance, void *mem) {
	test *instance = (test*) vinstance;

	instance->delay_line = (float *)mem;
}

void tibia_reset(void *vinstance) {
	test *instance = (test*) vinstance;

	for (size_t i = 0; i < instance->delay_line_length; i++)
		instance->delay_line[i] = 0.f;
	instance->delay_line_cur = 0;
	instance->z1 = 0.f;
	instance->cutoff_k = 1.f;
	instance->yz1 = 0.f;
}

void tibia_set_parameter(void *vinstance, size_t index, float value) {
	test *instance = (test*) vinstance;

	switch (index) {
	case parameter_gain:
		instance->gain = value;
		break;
	case parameter_delay:
		instance->delay = value < 0.f ? 0.f : value > 1000.f ? 1000.f : value;
		break;
	case parameter_cutoff:
		instance->cutoff = value;
		break;
	case parameter_bypass:
		instance->bypass = value >= 0.5f;
		break;
	}
}

float tibia_get_parameter(void *vinstance, size_t index) {
	test *instance = (test*) vinstance;

	(void)index;
	return instance->yz1;
}

static size_t calc_index(size_t cur, size_t delay, size_t len) {
	return (cur < delay ? cur + len : cur) - delay;
}

void tibia_process(void *vinstance, const float **inputs, float **outputs, size_t n_samples) {
	test *instance = (test*) vinstance;

	//approx const float gain = powf(10.f, 0.05f * instance->gain);
	const float gain = ((2.6039890429412597e-4f * instance->gain + 0.032131027163547855f) * instance->gain + 1.f) / ((0.0012705124328080768f * instance->gain - 0.0666763481312185f) * instance->gain + 1.f);
	//approx const size_t delay = roundf(instance->sample_rate * 0.001f * instance->delay);
	const size_t delay = (size_t)(instance->sample_rate * 0.001f * instance->delay + 0.5f);
	const float mA1 = instance->sample_rate / (instance->sample_rate + 6.283185307179586f * instance->cutoff * instance->cutoff_k);
	for (size_t i = 0; i < n_samples; i++) {
		instance->delay_line[instance->delay_line_cur] = inputs[0][i];
		const float x = instance->delay_line[calc_index(instance->delay_line_cur, delay, instance->delay_line_length)];
		instance->delay_line_cur++;
		if (instance->delay_line_cur == instance->delay_line_length)
			instance->delay_line_cur = 0;
		const float y = x + mA1 * (instance->z1 - x);
		instance->z1 = y;
		outputs[0][i] = instance->bypass ? inputs[0][i] : gain * y;
		instance->yz1 = outputs[0][i];
	}
}

void tibia_midi_msg_in(void *vinstance, size_t index, const uint8_t * data) {
	test *instance = (test*) vinstance;

	(void)index;
	if (((data[0] & 0xf0) == 0x90) && (data[2] != 0))
		//approx instance->cutoff_k = powf(2.f, (1.f / 12.f) * (note - 60));
		instance->cutoff_k = data[1] < 64 ? (-0.19558034980097166f * data[1] - 2.361735109225749f) / (data[1] - 75.57552349522389f) : (393.95397927344214f - 7.660826245588588f * data[1]) / (data[1] - 139.0755234952239f);
}

static void serialize_float(uint8_t *dest, float f) {
	union { float f; uint32_t u; } v;
	v.f = f;
	dest[0] = v.u & 0xff;
	dest[1] = (v.u & 0xff00) >> 8;
	dest[2] = (v.u & 0xff0000) >> 16;
	dest[3] = (v.u & 0xff000000) >> 24;
}

static float parse_float(const uint8_t *data) {
	union { float f; uint32_t u; } v;
	v.u = data[0];
	v.u |= data[1] << 8;
	v.u |= data[2] << 16;
	v.u |= (uint32_t)data[3] << 24;
	return v.f;
}

int tibia_state_save(void *vinstance, const tibia_state_callbacks *cbs, float last_sample_rate) {
	test *instance = (test*) vinstance;

	(void)last_sample_rate;
	uint8_t data[13];
	cbs->lock(cbs->handle);
	const float gain = instance->gain;
	const float delay = instance->delay;
	const float cutoff = instance->cutoff;
	const char bypass = instance->bypass;
	cbs->unlock(cbs->handle);
	serialize_float(data, gain);
	serialize_float(data + 4, delay);
	serialize_float(data + 8, cutoff);
	data[12] = bypass ? 1 : 0;
	return cbs->write(cbs->handle, (const char *)data, 13);
}

static char x_isnan(float x) {
	union { uint32_t u; float f; } v;
	v.f = x;
	return ((v.u & 0x7f800000) == 0x7f800000) && (v.u & 0x7fffff);
}

int tibia_state_load(const tibia_state_callbacks *cbs, float cur_sample_rate, const char *data, size_t length) {
	(void)cur_sample_rate;
	if (length != 13)
		return -1;
	const uint8_t *d = (const uint8_t *)data;
	const float gain = parse_float(d);
	const float delay = parse_float(d + 4);
	const float cutoff = parse_float(d + 8);
	const float bypass = d[12] ? 1.f : 0.f;
	if (x_isnan(gain) || x_isnan(delay) || x_isnan(cutoff))
		return -1;
	cbs->lock(cbs->handle);
	cbs->set_parameter(cbs->handle, parameter_gain, gain);
	cbs->set_parameter(cbs->handle, parameter_delay, delay);
	cbs->set_parameter(cbs->handle, parameter_cutoff, cutoff);
	cbs->set_parameter(cbs->handle, parameter_bypass, bypass);
	cbs->unlock(cbs->handle);
	return 0;
}
