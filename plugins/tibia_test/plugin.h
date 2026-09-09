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

typedef struct {
	float sample_rate;
	size_t delay_line_length;

	float gain;
	float delay;
	float cutoff;
	char bypass;

	float *delay_line;
	size_t delay_line_cur;
	float z1;
	float cutoff_k;
	float yz1;
} test;

typedef test plugin;

static int plugin_init(void *vinstance, const plugin_callbacks *cbs) {
	*(test *)vinstance = (test){0};
	(void)cbs;
	return 0;
}

static void plugin_fini(void *vinstance) {
	(void)vinstance;
}

static void plugin_set_sample_rate(void *vinstance, float sample_rate) {
	test *instance = (test *)vinstance;

	instance->sample_rate = sample_rate;
	//safe approx instance->delay_line_length = ceilf(sample_rate) + 1;
	instance->delay_line_length = (size_t)(sample_rate + 1.f) + 1;
}

static size_t plugin_mem_req(void *vinstance) {
	test *instance = (test *)vinstance;

	return instance->delay_line_length * sizeof(float);
}

static void plugin_mem_set(void *vinstance, void *mem) {
	test *instance = (test *)vinstance;

	instance->delay_line = (float *)mem;
}

static void plugin_reset(void *vinstance) {
	test *instance = (test *)vinstance;

	for (size_t i = 0; i < instance->delay_line_length; i++)
		instance->delay_line[i] = 0.f;
	instance->delay_line_cur = 0;
	instance->z1 = 0.f;
	instance->cutoff_k = 1.f;
	instance->yz1 = 0.f;
}

static void plugin_set_parameter(void *vinstance, size_t index, float value) {
	test *instance = (test *)vinstance;

	switch (index) {
	case plugin_parameter_gain:
		instance->gain = value;
		break;
	case plugin_parameter_delay:
		instance->delay = value < 0.f ? 0.f : value > 1000.f ? 1000.f : value;
		break;
	case plugin_parameter_cutoff:
		instance->cutoff = value;
		break;
	case plugin_parameter_bypass:
		instance->bypass = value >= 0.5f;
		break;
	}
}

static float plugin_get_parameter(void *vinstance, size_t index) {
	test *instance = (test *)vinstance;

	(void)index;
	return instance->yz1;
}

static size_t calc_index(size_t cur, size_t delay, size_t len) {
	return (cur < delay ? cur + len : cur) - delay;
}

static void plugin_process(void *vinstance, const float **inputs, float **outputs, size_t n_samples) {
	test *instance = (test *)vinstance;

	//approx const float gain = powf(10.f, 0.05f * instance->gain);
	const float gain = ((2.6039890429412597e-4f * instance->gain + 0.032131027163547855f) * instance->gain + 1.f) /
	    ((0.0012705124328080768f * instance->gain - 0.0666763481312185f) * instance->gain + 1.f);
	//approx const size_t delay = roundf(instance->sample_rate * 0.001f * instance->delay);
	const size_t delay = (size_t)(instance->sample_rate * 0.001f * instance->delay + 0.5f);
	const float mA1 =
	    instance->sample_rate / (instance->sample_rate + 6.283185307179586f * instance->cutoff * instance->cutoff_k);
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

static void plugin_midi_msg_in(void *vinstance, size_t index, const uint8_t *data) {
	test *instance = (test *)vinstance;

	(void)index;
	if (((data[0] & 0xf0) == 0x90) && (data[2] != 0))
		//approx instance->cutoff_k = powf(2.f, (1.f / 12.f) * (note - 60));
		instance->cutoff_k = data[1] < 64
		    ? (-0.19558034980097166f * data[1] - 2.361735109225749f) / (data[1] - 75.57552349522389f)
		    : (393.95397927344214f - 7.660826245588588f * data[1]) / (data[1] - 139.0755234952239f);
}
