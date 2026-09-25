// Integration check using the real SoundFont: polyphony, velocity, sustain,
// note-off, variable block sizes, reset and multiple sample rates.
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "plugin_api.h"
#include "plugin.h"

static const char *directory(void *handle) { return handle; }
static void midi(plugin *p, int type, int key, int velocity) {
    uint8_t message[3] = {type, key, velocity};
    plugin_midi_msg_in(p, 0, message);
}
static double render(plugin *p, size_t count) {
    float left[1026], right[1026];
    assert(count <= 1025);
    left[count] = right[count] = 12345.f;
    float *out[] = {left, right};
    plugin_process(p, NULL, out, count);
    assert(left[count] == 12345.f && right[count] == 12345.f);
    double energy = 0;
    for (size_t i = 0; i < count; ++i) {
        assert(isfinite(left[i]) && isfinite(right[i]));
        energy += left[i] * left[i] + right[i] * right[i];
    }
    return energy;
}
int main(void) {
    plugin p;
    plugin_callbacks bad = { .handle = "/missing-soundfont", .get_datadir = directory };
    assert(plugin_init(&p, &bad) != 0);
    plugin_fini(&p);
    plugin_callbacks c = { .handle = "build/plugin.perone", .get_datadir = directory };
    assert(plugin_init(&p, &c) == 0);
    assert(plugin_mem_req(&p) == 0);
    plugin_mem_set(&p, NULL);
    const int rates[] = {32000, 44100, 48000};
    for (size_t r = 0; r < sizeof(rates) / sizeof(*rates); ++r) {
        plugin_set_sample_rate(&p, rates[r]);
        plugin_reset(&p);
        assert(render(&p, 1025) == 0);
        midi(&p, 0x90, 60, 45);
        double soft = render(&p, 1025);
        plugin_reset(&p);
        midi(&p, 0x90, 60, 112);
        double loud = render(&p, 1025);
        assert(soft > 0 && loud > soft * 2);
        const int chord[] = {36, 48, 55, 59, 64, 67, 72, 76};
        for (size_t i = 0; i < sizeof(chord) / sizeof(*chord); ++i)
            midi(&p, 0x90, chord[i], 92);
        assert(tsf_active_voice_count(p.font) >= 18);
        assert(render(&p, 1) > 0);
        assert(render(&p, 127) > 0);
        assert(render(&p, 256) > 0);
        midi(&p, 0xb0, 64, 127);
        midi(&p, 0x90, 60, 0); // Velocity zero is note-off.
        for (size_t i = 0; i < sizeof(chord) / sizeof(*chord); ++i)
            midi(&p, 0x80, chord[i], 0);
        for (int i = 0; i < 30; ++i) render(&p, 1025);
        assert(tsf_active_voice_count(p.font) >= 18);
        midi(&p, 0xb0, 64, 0);
        for (int i = 0; i < rates[r] * 4 / 1025 + 1; ++i) render(&p, 1025);
        assert(tsf_active_voice_count(p.font) == 0);
        plugin_set_parameter(&p, plugin_parameter_detune, 19.f);
        plugin_set_parameter(&p, plugin_parameter_blend, .2f);
        assert(plugin_get_parameter(&p, plugin_parameter_detune) == 19.f);
        midi(&p, 0x90, 69, 100);
        assert(render(&p, 1025) > 0);
        plugin_reset(&p);
        assert(render(&p, 1025) == 0);
        printf("piano: %d Hz, polyphony, velocity, sustain, release, reset OK\n", rates[r]);
    }
    plugin_fini(&p);
    plugin_fini(&p);
    return 0;
}
