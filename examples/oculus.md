# Oculus

A two-part techno trance score in 4/4 at 150 BPM, lasting 161.2 seconds.
Four-on-the-floor drums, sixteenth-note bass, offbeat sub, detuned leads, acid
sequences and fast arpeggios surround both complete voices of the supplied MIDI.

## Source and form

The original is [sources/oculus-non-vidit.mid](sources/oculus-non-vidit.mid), SHA-256
`a63915630cbaf48e5b886df91332828e935cff2f08a3453a2c3764207c59724e`.
The [Janet transcription](sources/oculus-non-vidit.janet) stores note attacks,
durations in quarter notes and MIDI pitches; no MIDI parser runs during playback.
P1 has 108 notes. P2 has 119 and enters at beat 6.

Each voice uses two detuned synths. P1 is mostly sawtooth and left of center;
P2 has a pulse core and sits mostly right. Both 32-bar statements preserve the
source pitches, register, order and imitative entry. The final MIDI hold is
shortened from 8.073 to 8 beats to fit the 128-beat cell. The MIDI's slowing
cadence is played at the arrangement's fixed tempo. The acid section doubles
the speed of an excerpt from both parts; bass and arpeggios are new material.

| Time | Bars | Section |
| --- | --- | --- |
| 0:00 | 1–4 | Ignition: voices enter and filters open. |
| 0:06 | 5–36 | Duet: complete MIDI over drums and bass. |
| 0:58 | 37–52 | Acid: both parts at double speed, moving filters and accents. |
| 1:23 | 53–60 | Break: central excerpt with sparse drums. |
| 1:36 | 61–64 | Build: cadence, rising noise, snare roll and a brief stop. |
| 1:42 | 65–96 | Rave: second complete statement, arpeggios and triplets. |
| 2:34 | 97–100 | End: original cadence, final D and decay. |

## Playback and export

Build the host and local `synth_mono`, `drums`, `shape` and `echo` plugins, then run:

```sh
./build/cli --play examples/oculus.janet 48000
mkdir -p renders
./build/cli examples/oculus.janet renders/oculus.wav 48000
./build/gui examples/oculus.janet
```

The export is stereo PCM16, normalized to a peak of 0.94. Master saturation also
applies during playback. For the browser, build the same plugins for `wasm32`
and run `make web`.
