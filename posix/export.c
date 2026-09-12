#include "daw.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
	FILE *file;
	int failed;
} WavFile;

static ma_result wav_write(ma_encoder *encoder, const void *data, size_t size, size_t *written) {
	WavFile *wav = encoder->pUserData;
	*written = fwrite(data, 1, size, wav->file);
	wav->failed |= *written != size;
	return wav->failed ? MA_IO_ERROR : MA_SUCCESS;
}

static ma_result wav_seek(ma_encoder *encoder, ma_int64 offset, ma_seek_origin origin) {
	WavFile *wav = encoder->pUserData;
	int whence = origin == ma_seek_origin_start ? SEEK_SET : origin == ma_seek_origin_end ? SEEK_END : SEEK_CUR;
	wav->failed |= fseeko(wav->file, offset, whence) != 0;
	return wav->failed ? MA_IO_ERROR : MA_SUCCESS;
}

int write_score(Session *s, const Output *cfg, const char *path) {
	if (!s->sealed || s->time) {
		s->error = "export requires a fresh, sealed session";
		return -1;
	}
	s->error = NULL;
	char *temporary = malloc(strlen(path) + sizeof(".XXXXXX"));
	if (!temporary) {
		s->error = "out of memory";
		return -1;
	}
	sprintf(temporary, "%s.XXXXXX", path);
	int result = -1, fd = mkstemp(temporary);
	FILE *spool = NULL;
	WavFile wav = {0};
	if (fd < 0)
		goto done;
	wav.file = fdopen(fd, "w+b");
	if (!wav.file) {
		close(fd);
		goto done;
	}
	// Both passes stream to disk; the destination is replaced only after a successful close.
	spool = cfg->normalize ? tmpfile() : NULL;
	if (cfg->normalize && !spool)
		goto done;
	float peak = 0, audio[BLOCK * 2];
	if (spool) {
		while (s->time < s->frames) {
			size_t n = s->frames - s->time < BLOCK ? s->frames - s->time : BLOCK;
			if (session_render(s, audio, n) || fwrite(audio, sizeof(float) * 2, n, spool) != n)
				goto done;
			for (size_t i = 0; i < n * 2; ++i)
				peak = fmaxf(peak, fabsf(audio[i]));
		}
		if (fseek(spool, 0, SEEK_SET))
			goto done;
	}
	ma_encoder encoder;
	ma_encoder_config config =
	    ma_encoder_config_init(ma_encoding_format_wav, cfg->pcm16 ? ma_format_s16 : ma_format_f32, 2, session_rate(s));
	if (ma_encoder_init(wav_write, wav_seek, &wav, &config, &encoder) != MA_SUCCESS)
		goto done;
	result = 0;
	for (size_t pos = 0; pos < s->frames;) {
		size_t n = s->frames - pos < BLOCK ? s->frames - pos : BLOCK;
		if (spool ? fread(audio, sizeof(float) * 2, n, spool) != n : session_render(s, audio, n)) {
			result = -1;
			break;
		}
		if (spool && peak)
			for (size_t i = 0; i < 2 * n; ++i)
				audio[i] = (audio[i] / peak) * cfg->normalize;
		int16_t pcm[BLOCK * 2];
		if (cfg->pcm16)
			for (size_t i = 0; i < 2 * n; ++i)
				pcm[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, audio[i])) * 32767);
		ma_uint64 written;
		if (ma_encoder_write_pcm_frames(&encoder, cfg->pcm16 ? (const void *)pcm : audio, n, &written) != MA_SUCCESS ||
		    written != n) {
			result = -1;
			break;
		}
		pos += n;
	}
	ma_encoder_uninit(&encoder);
done:
	if (spool)
		fclose(spool);
	if (wav.file && fclose(wav.file))
		result = -1;
	if (wav.failed)
		result = -1;
	if (!result && rename(temporary, path))
		result = -1;
	if (result) {
		if (fd >= 0)
			unlink(temporary);
		if (!s->error)
			s->error = "cannot write WAV";
	}
	free(temporary);
	return result;
}
