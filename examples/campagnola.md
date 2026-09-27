# Campagnola Stomp

A solo honky-tonk piano arrangement of the instrumental theme from the supplied
Gigione MIDI, *A campagnola*. It has 68 bars in 4/4, around 138 seconds including
the final decay. The main tempo is 124 BPM; the trio relaxes to about 115 BPM and
the last refrain pushes to about 127 BPM.

The piano writing draws on Jelly Roll Morton's *The Crave*: habanera, stride,
syncopation, chromatic grace notes and shifts between major and minor. Its melody
is not quoted. The piano's second layer is detuned by +12 cents at 34% volume.
Timing and velocity variations are deterministic; the sound is dry.

## Source and form

The MIDI doubles its brass melody in octaves on channel 0. The transcription
keeps the upper note of each attack: beats 4–36 for the theme and 60–92 for the
chromatic response, at 192 ticks per quarter note. Original notes, durations and
the source SHA-256 are in [sources/campagnola-theme.janet](sources/campagnola-theme.janet).

The first statement preserves the theme's pitches and attacks, shortening long
notes for piano articulation. Later statements change register, hand and
accompaniment. The bass, harmony, introduction, trio, bridge and coda are new.
Unisons between the hands are merged, and each key releases before its next strike.

| Time | Bars | Section |
| --- | --- | --- |
| 0:00 | 1–4 | Habanera introduction and chromatic pickup. |
| 0:08 | 5–12 | Complete theme over habanera. |
| 0:23 | 13–20 | Ragtime reprise with inversions and lighter Charleston accompaniment. |
| 0:39 | 21–28 | Chromatic response, register jumps and moving bass. |
| 0:54 | 29–36 | Lyrical trio between minor and relative major, with a slower tempo. |
| 1:11 | 37–44 | B-flat bridge and a chain of dominants returning to G. |
| 1:26 | 45–48 | Stop-time calls and bass responses. |
| 1:34 | 49–56 | Theme passed between left hand, right hand and upper register. |
| 1:50 | 57–64 | Octave refrain, rising register and a slight acceleration. |
| 2:05 | 65–68 | Coda, slowing cadence and two final chords. |

## Playback and export

Build the optional [sampled piano](../plugins/piano/README.md), then run:

```sh
mkdir -p renders
./build/cli examples/campagnola.janet renders/campagnola-stomp.wav 48000
./build/cli --play examples/campagnola.janet 48000
./build/gui examples/campagnola.janet
```

The export is stereo PCM16, normalized to a peak of 0.94. For browser playback,
also build the piano with `PERONE_PLATFORM=wasm32` and run `make web`.
Set `CAMPAGNOLA_EXPORT=renders/campagnola-events.csv` on the render command to
export note start/end in quarter notes, MIDI pitch, velocity and hand (0 left,
1 right). The exported times include all tempo changes.
