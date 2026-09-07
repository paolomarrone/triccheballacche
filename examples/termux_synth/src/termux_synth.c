#define MA_NO_NULL
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define TAU 6.28318530717958647692f
enum { MAX_VOICES = 16, QUEUE = 64 };
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Audio requires lock-free unsigned atomics");

typedef struct {
    float freq, phase, age, release_at, velocity;
    int active;
} Voice;
typedef struct {
    Voice voices[MAX_VOICES], pending[QUEUE];
    atomic_uint head, tail; // Main thread produces; audio thread consumes.
    float sample_rate, gain;
} Synth;
static volatile sig_atomic_t stopped;

static void on_signal(int sig) { (void)sig; stopped = 1; }

static void trigger_note(Synth *s, float freq, float velocity, float duration) {
    unsigned head = atomic_load_explicit(&s->head, memory_order_relaxed);
    unsigned next = (head + 1) % QUEUE;
    if (next == atomic_load_explicit(&s->tail, memory_order_acquire)) return;
    s->pending[head] = (Voice){.freq = freq, .velocity = velocity, .release_at = duration, .active = 1};
    atomic_store_explicit(&s->head, next, memory_order_release);
}

static float voice_sample(Voice *v, float dt) {
    v->age += dt;
    v->phase = fmodf(v->phase + TAU * v->freq * dt, 2.f * TAU);
    float age = fminf(v->age, v->release_at);
    float env = age < .012f ? age / .012f : .85f * expf(-1.7f * (age - .012f)) + .15f;
    if (v->age >= v->release_at) {
        env *= expf(-7.f * (v->age - v->release_at));
        if (env < .001f) { v->active = 0; return 0; }
    }
    return (sinf(v->phase) + .25f * sinf(.5f * v->phase)) * env * v->velocity;
}

static void audio_callback(ma_device *device, void *output, const void *input, ma_uint32 frames) {
    Synth *s = device->pUserData;
    float *out = output;
    (void)input;
    unsigned tail = atomic_load_explicit(&s->tail, memory_order_relaxed);
    unsigned head = atomic_load_explicit(&s->head, memory_order_acquire);
    while (tail != head) {
        int slot = 0;
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!s->voices[i].active) { slot = i; break; }
            if (s->voices[i].age > s->voices[slot].age) slot = i;
        }
        s->voices[slot] = s->pending[tail];
        tail = (tail + 1) % QUEUE;
    }
    atomic_store_explicit(&s->tail, tail, memory_order_release);
    for (ma_uint32 i = 0; i < frames; ++i) {
        float sample = 0;
        for (int v = 0; v < MAX_VOICES; ++v)
            if (s->voices[v].active) sample += voice_sample(&s->voices[v], 1.f / s->sample_rate);
        sample = fmaxf(-1.f, fminf(1.f, sample * s->gain));
        out[2 * i] = out[2 * i + 1] = sample;
    }
}

static float note_freq(int note) { return 440.f * exp2f((note - 69) / 12.f); }

static void play_demo(Synth *s) {
    static const int notes[] = {60, 64, 67, 72, 67, 64, 62, 65, 69, 74, 72, 67, 64, 67, 71, 76};
    for (int i = 0; i < 32 && !stopped; ++i) {
        float freq = note_freq(notes[i % 16]);
        trigger_note(s, freq, .85f, .22f);
        if (i % 4 == 0) trigger_note(s, .5f * freq, .55f, .42f);
        ma_sleep(180);
    }
    for (int i = 0; i < 120 && !stopped; ++i) ma_sleep(10);
}

static void play_keys(Synth *s) {
    struct termios original, raw;
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &original)) {
        fputs("Keyboard needs a terminal; playing demo.\n", stderr);
        play_demo(s);
        return;
    }
    raw = original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw)) { perror("terminal"); return; }
    puts("keys: a w s e d f t g y h u j k   q: quit");
    while (!stopped) {
        char key;
        if (read(STDIN_FILENO, &key, 1) == 1) {
            if (key == 'q' || key == 27) break;
            const char *keys = "awsedftgyhujk";
            const char *p = key ? strchr(keys, key) : NULL;
            if (p) trigger_note(s, note_freq(60 + (int)(p - keys)), .9f, .35f);
        }
        ma_sleep(8);
    }
    if (tcsetattr(STDIN_FILENO, TCSANOW, &original)) perror("terminal restore");
}

int main(int argc, char **argv) {
    int keys = argc == 2 && (!strcmp(argv[1], "--keys") || !strcmp(argv[1], "-k"));
    if (argc > 2 || (argc == 2 && !keys)) {
        fprintf(stderr, "Usage: %s [--keys]\n", argv[0]);
        return 1;
    }
    Synth synth = {.sample_rate = 48000.f, .gain = .18f};
    atomic_init(&synth.head, 0);
    atomic_init(&synth.tail, 0);
    ma_device device;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = (ma_uint32)synth.sample_rate;
    cfg.dataCallback = audio_callback;
    cfg.pUserData = &synth;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    ma_result err = ma_device_init(NULL, &cfg, &device);
    if (err != MA_SUCCESS) { fprintf(stderr, "Audio init: %s\n", ma_result_description(err)); return 1; }
    err = ma_device_start(&device);
    if (err == MA_SUCCESS) {
        if (keys) play_keys(&synth); else play_demo(&synth);
    } else fprintf(stderr, "Audio start: %s\n", ma_result_description(err));
    ma_device_uninit(&device);
    return err != MA_SUCCESS;
}
