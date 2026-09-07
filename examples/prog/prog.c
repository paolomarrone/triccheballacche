// Il polpo a sette gomiti — a 30-second miniature for an imaginary prog band.
#define main host_main
#include "../../main.c"
#undef main
#include <assert.h>
#include <math.h>

enum { TRACKS = 8, EVENTS = 2048, SECONDS = 30, FRAMES = SECONDS * SAMPLE_RATE };
enum { BASS, LEFT, RIGHT, LEAD, KEY1, KEY2, KEY3, COUNTER };
enum { KICK, SNARE, HAT, OPEN_HAT, CRASH, TOM_HIGH, TOM_LOW };
static Engine band[TRACKS];
static Event score[TRACKS][EVENTS];
static float *mix, *send;
static uint32_t seed = 0x706f6c70;
static const float tau = 6.28318530718f;

static float noise(void) {
	seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
	return (seed >> 8) * (2.f / 16777216.f) - 1.f;
}
static void event(int track, double time, int parameter, float value, int status, int note) {
	Engine *e = band + track;
	assert(e->count < EVENTS && time >= 0 && time < SECONDS);
	score[track][e->count++] = (Event){(size_t)llround(time * SAMPLE_RATE), parameter, value,
		{(uint8_t)status, (uint8_t)note, status == 0x90 ? 100 : 0}};
}
static void param(int track, double t, int index, float value) { event(track, t, index, value, 0, 0); }
static void note(int track, double t, double length, int pitch, float volume) {
	param(track, t, 0, volume);
	event(track, t, -1, 0, 0x90, pitch);
	event(track, t + length, -1, 0, 0x80, pitch);
}
static int compare(const void *aa, const void *bb) {
	const Event *a = aa, *b = bb;
	if (a->time != b->time) return a->time < b->time ? -1 : 1;
	int pa = a->parameter >= 0 ? 0 : a->midi[0];
	int pb = b->parameter >= 0 ? 0 : b->midi[0];
	return (pa > pb) - (pa < pb); // Controls, note off, then note on.
}
static void pan_add(float *dst, size_t frame, float sample, float pan) {
	float angle = (pan + 1.f) * tau / 8.f;
	dst[2 * frame] += sample * cosf(angle);
	dst[2 * frame + 1] += sample * sinf(angle);
}

static void drum(int type, double time, float strength) {
	const float duration[] = {.42f, .32f, .085f, .38f, 1.6f, .3f, .4f};
	const float pans[] = {0, -.08f, .35f, .4f, -.55f, -.4f, .45f};
	size_t start = (size_t)llround(time * SAMPLE_RATE), n = duration[type] * SAMPLE_RATE;
	float low = 0;
	for (size_t i = 0; i < n && start + i < FRAMES; ++i) {
		float t = (float)i / SAMPLE_RATE, white = noise(), x;
		low += .17f * (white - low);
		float high = white - low;
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
		x *= strength * fminf(1.f, t * 1800.f);
		pan_add(mix, start + i, x, pans[type]);
		if (type != KICK) pan_add(send, start + i, .07f * x, pans[type]);
	}
}

static void patch(int track) {
	TibiaModule *m = band[track].module;
	void *p = band[track].instance;
	const float patches[TRACKS][8] = {
		// wave, cutoff, resonance, attack, decay, sustain, release, portamento
		{2, 700, 12, 2, 95, 65, 35, 0},
		{1, 3800, 8, 2, 90, 30, 24, 0}, {1, 3500, 10, 2, 100, 32, 28, 0},
		{1, 4200, 18, 3, 110, 90, 70, 9},
		{3, 4800, 5, 8, 330, 42, 310, 0}, {3, 5200, 5, 8, 330, 42, 310, 0},
		{3, 5600, 5, 8, 330, 42, 310, 0}, {2, 3400, 24, 3, 120, 65, 80, 4}
	};
	const int ids[] = {7, 26, 27, 33, 34, 35, 36, 2};
	for (int i = 0; i < 8; ++i) m->set_parameter(p, ids[i], patches[track][i]);
	m->set_parameter(p, 8, track == COUNTER ? 28 : 47);
	m->set_parameter(p, 13, track == BASS ? 3 : 2);
	m->set_parameter(p, 11, track == BASS ? -1 : track >= KEY1 && track <= KEY3 ? 1 : 0);
	m->set_parameter(p, 12, track == LEFT ? -7 : track == RIGHT ? 7 : 3);
	m->set_parameter(p, 15, track == BASS ? 65 : track >= KEY1 && track <= KEY3 ? 45 : 52);
	m->set_parameter(p, 28, track == BASS ? 55 : 22);
	m->set_parameter(p, 30, 105);
	m->set_parameter(p, 31, 15);
	m->reset(p);
}

static int scale(int degree) {
	static const int steps[] = {0, 2, 3, 5, 7, 8, 11};
	return steps[degree % 7] + 12 * (degree / 7);
}
static void riff(double t, double length, int pitch, float volume) {
	note(LEFT, t, length, pitch, volume);
	note(RIGHT, t + .004, length * .97, pitch + 7, volume - 3);
}
static void chord(double t, double length, int root, int third, int seventh, float volume) {
	note(KEY1, t, length, root + 24, volume);
	note(KEY2, t + .006, length, root + 24 + third, volume - 2);
	note(KEY3, t + .011, length, root + 24 + seventh, volume - 4);
}

static double section(double start, int part, int bars, int eighths, double bpm) {
	static const int roots[][6] = {{40, 40, 41, 40}, {40, 43, 42, 35}, {36, 34},
		{40, 40, 38, 35, 36, 40}, {40, 41}};
	static const int hook[] = {0, 0, 7, 1, 0, 10, 4};
	static const int spiral[] = {0, 2, 4, 6, 5, 3, 1, 7, 4, 8, 6, 3, 9, 5};
	double step = 30.0 / bpm;
	for (int b = 0; b < bars; ++b) {
		double bar = start + b * eighths * step;
		int root = roots[part][b];
		if (b == 0 || (part == 3 && b == 4)) drum(CRASH, bar, .33f);
		chord(bar, step * (part == 2 ? 8.8 : 1.35), root, part == 2 ? 4 : 3, part == 2 ? 11 : 10,
			part == 2 ? 69 : 49);
		for (int j = 0; j < eighths; ++j) {
			double t = bar + j * step;
			int accent = j == 0 || j == 3 || j == eighths - 2;
			int rest = (part == 1 && b == bars - 1 && j == eighths - 1)
				|| (part == 4 && j == 8);
			if (rest) continue;
			drum(j == eighths - 1 ? OPEN_HAT : HAT, t, accent ? .16f : .10f);
			if (part != 2 && j % 2 == 0) drum(HAT, t + .5 * step, .055f);
			if (accent || (part == 3 && j == 4)) drum(KICK, t, accent ? .82f : .55f);
			if (j == 3 || (part != 2 && j == eighths - 1)) drum(SNARE, t + .002, .78f);
			if (j == 2 && part != 2) drum(SNARE, t + .72 * step, .17f);
			if (part == 2) {
				if (j % 2 == 0) note(BASS, t, 1.4 * step, root + (j == 6 ? 7 : 0), 68);
				static const int arp[] = {0, 7, 11, 14, 18, 14, 11, 7, 4, 11};
				note(COUNTER, t, .55 * step, root + 24 + arp[j], 56);
				if (j == 0) note(LEAD, t + step, 4.8 * step, root + 28, 64);
				if (j == 7) riff(t, 1.1 * step, root + 19, 52);
			} else {
				int interval = part == 1 ? (j < 7 ? hook[j] : j == 7 ? 6 : 11) : hook[j % 7];
				if (part == 3) interval = (j == 4 ? 7 : j == 6 ? 11 : 0);
				note(BASS, t, .72 * step, root + (accent ? 0 : interval), accent ? 77 : 68);
				riff(t, step * (accent ? .64 : .43), root + 12 + interval, accent ? 74 : 65);
				if (part == 0 && b % 2 && j >= 3)
					note(LEAD, t, .7 * step, root + 24 + hook[6 - j], 67);
				if (part == 1 && j % 3 == 0) {
					note(LEAD, t, .4 * step, root + 24 + scale((j + b) % 10), 67);
					note(COUNTER, t + .5 * step, .38 * step, root + 24 + scale((j + b + 2) % 10), 59);
				}
				if (part == 4) note(LEAD, t, .75 * step, root + 24 + hook[j % 7], 71);
			}
		}
		if (part == 3) {
			param(LEAD, bar, 26, 3300 + 430 * b);
			for (int j = 0; j < 14; ++j)
				note(LEAD, bar + j * step / 2, step * .46, root + 24 + scale(spiral[(j + 2 * b) % 14]), 68 + j % 3);
			if (b == 3 || b == 5)
				for (int j = 0; j < 6; ++j)
					note(COUNTER, bar + (5 + j / 3.0) * step, step * .26, root + 36 + scale(5 - j), 57);
		}
		if (b == bars - 1 || (part == 3 && b % 2)) {
			for (int j = 0; j < 4; ++j)
				drum(j < 2 ? TOM_HIGH : TOM_LOW, bar + (eighths - 1 + j * .25) * step, .45f + j * .06f);
		}
	}
	return start + bars * eighths * step;
}

static void compose(void) {
	double t = section(0, 0, 4, 7, 154);
	t = section(t, 1, 4, 9, 166);
	t = section(t, 2, 2, 10, 128);
	t = section(t, 3, 6, 7, 184);
	t = section(t, 4, 2, 11, 176);
	const int ending[] = {40, 42, 46, 47, 52};
	for (int i = 0; i < 5; ++i) {
		double hit = t + i * 30.0 / 176;
		note(BASS, hit, .09, ending[i] - (i == 4 ? 12 : 0), 78);
		riff(hit, .085, ending[i] + 12, 77);
		note(LEAD, hit, .085, ending[i] + 36, 69);
		drum(KICK, hit, .84f); drum(SNARE, hit, .56f);
	}
	t += 5 * 30.0 / 176;
	for (int tr = 0; tr < TRACKS; ++tr) param(tr, t, 36, 650);
	note(BASS, t, .62, 28, 79); riff(t, .56, 52, 76);
	note(LEAD, t, .66, 88, 62); chord(t, .72, 40, 4, 11, 66);
	drum(KICK, t, .95f); drum(SNARE, t, .85f); drum(CRASH, t, .55f);
}

int main(int argc, char **argv) {
	if (argc != 3) { fprintf(stderr, "Usage: %s synth.so output.wav\n", argv[0]); return 1; }
	int result = 1;
	mix = calloc(FRAMES * 2, sizeof(float)); send = calloc(FRAMES * 2, sizeof(float));
	if (!mix || !send) goto done;
	for (int tr = 0; tr < TRACKS; ++tr) {
		if (open_engine(band + tr, argv[1])) goto done;
		band[tr].events = score[tr]; patch(tr);
	}
	compose();
	const float pans[] = {0, -.78f, .78f, .08f, -.55f, 0, .55f, -.3f};
	const float gains[] = {.73f, .28f, .28f, .58f, .23f, .21f, .23f, .34f};
	for (int tr = 0; tr < TRACKS; ++tr) {
		qsort(score[tr], band[tr].count, sizeof(Event), compare);
		float low = 0, dc = 0;
		for (size_t pos = 0; pos < FRAMES;) {
			float buffer[BLOCK];
			size_t n = FRAMES - pos < BLOCK ? FRAMES - pos : BLOCK;
			render(band + tr, buffer, NULL, n);
			for (size_t i = 0; i < n; ++i) {
				float x = buffer[i];
				if (tr == LEFT || tr == RIGHT) { // Saturation and a small cabinet filter.
					x = tanhf(8 * x); dc += .013f * (x - dc); x -= dc;
					low += .34f * (x - low); x = low;
				} else if (tr == BASS) x = .7f * tanhf(2 * x);
				x *= gains[tr];
				pan_add(mix, pos + i, x, pans[tr]);
				pan_add(send, pos + i, x * (tr >= LEAD ? .3f : .045f), pans[tr]);
			}
			pos += n;
		}
	}
	float peak = 0, dc[2] = {0};
	const int delays[] = {4807, 10275, 16185};
	for (size_t i = 0; i < FRAMES; ++i) {
		float fade = fminf(1.f, (FRAMES - i) / (.65f * SAMPLE_RATE));
		for (int c = 0; c < 2; ++c) {
			float x = mix[2 * i + c];
			for (int d = 0; d < 3; ++d)
				if (i >= (size_t)delays[d]) x += send[2 * (i - delays[d]) + (c ^ (d & 1))] / (d + 1);
			x = tanhf(1.35f * x);
			dc[c] += .002f * (x - dc[c]);
			x = (x - dc[c]) * fade * fade;
			if (!isfinite(x)) { fputs("Non-finite audio\n", stderr); goto done; }
			mix[2 * i + c] = x; peak = fmaxf(peak, fabsf(x));
		}
	}
	if (peak == 0) goto done;
	ma_encoder encoder;
	ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 2, SAMPLE_RATE);
	if (ma_encoder_init_file(argv[2], &cfg, &encoder) != MA_SUCCESS) goto done;
	result = 0;
	for (size_t pos = 0; pos < FRAMES;) {
		int16_t pcm[BLOCK * 2];
		size_t n = FRAMES - pos < BLOCK ? FRAMES - pos : BLOCK;
		for (size_t i = 0; i < 2 * n; ++i) pcm[i] = (int16_t)lrintf(mix[2 * pos + i] * (.94f * 32767 / peak));
		ma_uint64 written;
		if (ma_encoder_write_pcm_frames(&encoder, pcm, n, &written) != MA_SUCCESS || written != n) { result = 1; break; }
		pos += n;
	}
	ma_encoder_uninit(&encoder);
	if (!result) printf("Il polpo a sette gomiti: %d seconds, stereo, %s\n", SECONDS, argv[2]);
done:
	for (int tr = 0; tr < TRACKS; ++tr) close_engine(band + tr);
	free(mix); free(send);
	return result;
}
