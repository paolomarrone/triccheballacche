// Sampled piano with a quieter detuned unison. TinySoundFont is MIT licensed.
// Samples are loaded during preparation, never in the audio callback.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TSF_IMPLEMENTATION
#define TSF_STATIC
#include "tsf.h"

typedef struct {
    tsf *font;
    float gain, detune, blend;
    float scratch[512];
} plugin;

static int plugin_init(void *p, const plugin_callbacks *c) {
    plugin *v = p;
    *v = (plugin){ .gain = 0.6f, .detune = 11.f, .blend = 0.32f };
    const char *dir = c->get_datadir ? c->get_datadir(c->handle) : NULL;
    if (!dir) return -1;
    size_t n = strlen(dir) + sizeof("/piano.sf2");
    char *path = malloc(n);
    if (!path) return -1;
    snprintf(path, n, "%s/piano.sf2", dir);
    v->font = tsf_load_filename(path);
    free(path);
    if (!v->font) return -1;
    if (!tsf_set_max_voices(v->font, 192)
        || !tsf_channel_set_presetindex(v->font, 0, 0)
        || !tsf_channel_set_presetindex(v->font, 1, 0)) {
        tsf_close(v->font);
        v->font = NULL;
        return -1;
    }
    return 0;
}

static void plugin_fini(void *p) {
    plugin *v = p;
    if (v->font) tsf_close(v->font);
    v->font = NULL;
}
static void plugin_set_sample_rate(void *p, float rate) {
    tsf_set_output(((plugin *)p)->font, TSF_STEREO_UNWEAVED, (int)rate, -8.f);
}
static size_t plugin_mem_req(void *p) { (void)p; return 0; }
static void plugin_mem_set(void *p, void *mem) { (void)p; (void)mem; }
static void plugin_reset(void *p) {
    plugin *v = p;
    // Clear voices without freeing/reallocating the preallocated channel array.
    for (int i = 0; i < v->font->voiceNum; ++i)
        v->font->voices[i].playingPreset = -1;
    for (int ch = 0; ch < 2; ++ch) {
        tsf_channel_set_sustain(v->font, ch, 0);
        tsf_channel_set_pan(v->font, ch, ch ? .55f : .45f);
        tsf_channel_set_tuning(v->font, ch, ch ? v->detune * .01f : 0.f);
        tsf_channel_set_volume(v->font, ch, ch ? v->blend : 1.f);
    }
}
static void plugin_set_parameter(void *p, size_t i, float x) {
    plugin *v = p;
    if (!isfinite(x)) return;
    switch (i) {
    case plugin_parameter_gain: v->gain = fmaxf(0.f, fminf(2.f, x)); break;
    case plugin_parameter_detune:
        v->detune = fmaxf(0.f, fminf(30.f, x));
        tsf_channel_set_tuning(v->font, 1, v->detune * .01f); break;
    case plugin_parameter_blend:
        v->blend = fmaxf(0.f, fminf(1.f, x));
        tsf_channel_set_volume(v->font, 1, v->blend); break;
    }
}
static float plugin_get_parameter(void *p, size_t i) {
    plugin *v = p;
    switch (i) {
    case plugin_parameter_gain: return v->gain;
    case plugin_parameter_detune: return v->detune;
    case plugin_parameter_blend: return v->blend;
    default: return 0.f;
    }
}
static void plugin_process(void *p, const float **in, float **out, size_t n) {
    plugin *v = p;
    (void)in;
    for (size_t pos = 0; pos < n;) {
        size_t count = n - pos < 256 ? n - pos : 256;
        tsf_render_float(v->font, v->scratch, (int)count, 0);
        for (size_t i = 0; i < count; ++i) {
            out[0][pos + i] = v->scratch[i] * v->gain;
            out[1][pos + i] = v->scratch[count + i] * v->gain;
        }
        pos += count;
    }
}
static void plugin_midi_msg_in(void *p, size_t bus, const uint8_t *data) {
    plugin *v = p;
    (void)bus;
    int kind = data[0] & 0xf0;
    for (int ch = 0; ch < 2; ++ch) {
        if (kind == 0x90 && data[2])
            tsf_channel_note_on(v->font, ch, data[1], data[2] / 127.f);
        else if (kind == 0x80 || kind == 0x90)
            tsf_channel_note_off(v->font, ch, data[1]);
        else if (kind == 0xb0 && data[1] == 64)
            tsf_channel_set_sustain(v->font, ch, data[2] >= 64);
        else if (kind == 0xb0 && data[1] == 123)
            tsf_channel_note_off_all(v->font, ch);
        else if (kind == 0xb0 && data[1] == 120)
            tsf_channel_sounds_off_all(v->font, ch);
    }
}
