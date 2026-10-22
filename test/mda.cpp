// Exercise the source adapter directly, including reset and reconfiguration.
#include <cassert>
#include <initializer_list>
#include <cmath>
#include "plugin_api.h"
#include "plugin_cxx.h"

int main() {
	plugin p;
	assert(!plugin_init(&p, nullptr));
	float defaults[plugin_parameter__count];
	int count = p.dsp->numParams;
	assert(count <= plugin_parameter__count);
	for (int i = 0; i < count; ++i)
		defaults[i] = p.values[i];
	for (float rate : {44100.f, 48000.f, 96000.f}) {
		plugin_set_sample_rate(&p, rate);
		plugin_reset(&p);
		assert(p.dsp->getSampleRate() == rate);
		for (int param = -1; param < count; ++param) {
			for (float value : {0.f, .5f, 1.f}) {
				if (param >= 0)
					plugin_set_parameter(&p, param, value);
				plugin_reset(&p);
				if (p.dsp->getNumMidiInputChannels()) {
					// More simultaneous events than the original VST event buffer held.
					for (int note = 48; note < 112; ++note) {
						uint8_t data[] = {0x90, (uint8_t)note, 96};
						plugin_midi_msg_in(&p, 0, data);
					}
				}
				for (int n : {1, 31, 128, 257}) {
					float in[2][257], out[2][259];
					for (int ch = 0; ch < 2; ++ch) {
						for (int i = 0; i < n; ++i)
							in[ch][i] = .1f * std::sin(i * .13f + ch);
						out[ch][0] = out[ch][n + 1] = 123.f;
					}
					const float *inputs[] = {in[0], in[1]};
					float *outputs[] = {out[0] + 1, out[1] + 1};
					plugin_process(&p, inputs, outputs, n);
					for (int ch = 0; ch < 2; ++ch) {
						assert(out[ch][0] == 123.f && out[ch][n + 1] == 123.f);
						for (int i = 1; i <= n; ++i) {
							if (!std::isfinite(out[ch][i])) {
								fprintf(stderr, "Non-finite audio: rate %g, parameter %d = %g, block %d\n", rate, param,
								    value, n);
								return 1;
							}
						}
					}
				}
			}
			if (param >= 0)
				plugin_set_parameter(&p, param, defaults[param]);
		}
	}
	plugin_fini(&p);
}
