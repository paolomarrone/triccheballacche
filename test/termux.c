#define main synth_main
#include "../examples/termux_synth/src/termux_synth.c"
#undef main
#include <assert.h>

int main(void) {
    Synth s = {.sample_rate = 48000, .gain = .18f};
    atomic_init(&s.head, 0);
    atomic_init(&s.tail, 0);
    ma_device device = {.pUserData = &s};
    float out[256];
    trigger_note(&s, 440, .9f, .35f);
    audio_callback(&device, out, NULL, 128);
    assert(s.voices[0].active && s.voices[0].age > 0);
    assert(atomic_load(&s.head) == atomic_load(&s.tail));
    int audible = 0;
    for (int i = 0; i < 128; ++i) {
        assert(isfinite(out[2 * i]) && fabsf(out[2 * i]) <= 1);
        assert(out[2 * i] == out[2 * i + 1]);
        audible |= out[2 * i] != 0;
    }
    assert(audible);
    // Overflow discards new notes; queue wrap and voice stealing stay bounded.
    for (int round = 0; round < 100; ++round) {
        for (int i = 0; i < QUEUE * 2; ++i) trigger_note(&s, 440, .9f, .35f);
        audio_callback(&device, out, NULL, 128);
        assert(atomic_load(&s.head) == atomic_load(&s.tail));
        for (int i = 0; i < 256; ++i) assert(isfinite(out[i]) && fabsf(out[i]) <= 1);
    }
    for (int i = 0; i < 900; ++i) audio_callback(&device, out, NULL, 128);
    for (int i = 0; i < MAX_VOICES; ++i) assert(!s.voices[i].active);
    // The sub oscillator must alternate sign over two fundamental periods.
    Voice v = {.freq = 440, .velocity = 1, .release_at = 10, .active = 1};
    double mean[2] = {0};
    for (int i = 0; i < 2000; ++i) {
        voice_sample(&v, 1.f / 440000);
        mean[i / 1000] += sinf(.5f * v.phase);
    }
    assert(mean[0] > 600 && mean[1] < -600 && fabs(mean[0] + mean[1]) < 1);
    puts("OK: stereo synthesis, queue overflow/wrap, voice release, sub oscillator");
    return 0;
}
