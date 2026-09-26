# Embedded sampled piano

Polyphonic stereo piano for `examples/campagnola.janet`. The local C engine in
`piano.h` plays the original Florestan samples, with a quieter unison layer detuned
by `detune` cents. `blend` sets that layer's volume and `gain` controls the complete
instrument. The plugin has no external synthesis library or runtime file access.
The same source builds native and standalone Wasm bundles.

From the repository root:

```sh
python3 plugins/piano/fetch.py
make -C plugins/piano TIBIA=/absolute/path/to/tibia
make -C plugins/piano test TIBIA=/absolute/path/to/tibia
```

`fetch.py` downloads only Nando Florestan's
[000 Florestan Piano](https://dev.nando.audio/pages/soundfonts.html), checking both
the archive and extracted bank against SHA-256 hashes. It reuses a valid cached
bank offline. Python's standard library is sufficient. The original bank lives
in `.deps/florestan-piano/`; the samples retain their original attribution (the
bank's `ICOP` field identifies them as Public Domain).

During the build, `convert.py` extracts the bank's 20 stereo sample pairs and
360 key/velocity regions into `build/piano_data.h`. The generated header contains
PCM16 arrays and region metadata, is ignored by Git, and is embedded in each
plugin binary. Rebuilding from the cached bank requires no network. The roughly
7.5 MiB of PCM data is stored once per native module or Wasm instance; no separate
SoundFont file needs to travel with the bundle. `SOUNDFONT=/path/to/bank.sf2` can
select a bank supported by this converter, which is limited to a single preset
and explicitly rejects unsupported generators, custom modulators and invalid
sample ranges. It is not a general SoundFont implementation.

The engine provides linear sample interpolation, continuous/sustain loops,
volume and filter envelopes, a two-pole low-pass, velocity gain, MIDI note-off,
sustain CC64, all-notes-off CC123 and all-sounds-off CC120. Controllers respect
MIDI channels. All 192 sample voices are preallocated: with this stereo bank and
two unison layers, up to 48 notes can sound together, including release tails.
When full, it steals whole notes, preferring released voices and then the oldest.
Reset clears voices and pedal state while retaining parameter values.
The bank covers MIDI keys 0–108; notes outside its recorded mapping are silent.
Reverb and chorus sends in the bank are unused; the piano remains dry.

The original `gain`, `detune` and `blend` parameters and the score API are retained.
Bank attenuation is calibrated to the previous instrument's output level.
Envelopes are advanced per sample, with an 80 dB exponential decay/release;
filter updates use a voice-local clock. Rendering is independent of host block
size, with no allocation or file I/O during processing. Small differences from
the previous engine's envelope stepping mean old renders are not bit-identical.

For the browser, build the Wasm bundle and republish the project catalog:

```sh
make -C plugins/piano PERONE_PLATFORM=wasm32 TIBIA=/absolute/path/to/tibia
make web
```

Pass an absolute `EMCC` path if Emscripten is outside `PATH`. The piano's initial
Wasm memory is 16 MiB to accommodate its embedded bank. The plugin remains an
optional build; the catalog discovers it under `plugins/piano/build/plugin.perone`
when present. Build its Wasm binary before publishing it to the browser.

`make -C plugins/piano test` checks the converter, key/velocity mapping, stereo,
polyphony, pedal/release, retriggers, voice stealing, parameter changes, sample
rates and exact block-size independence. To also compare native and Wasm PCM
through the production Perone loader (Node.js and Emscripten required):

```sh
make -C plugins/piano test-web PERONE_PLATFORM=wasm32 TIBIA=/absolute/path/to/tibia
```
