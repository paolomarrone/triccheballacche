# Optional native piano

Polyphonic stereo sampled piano for `examples/campagnola.janet`. One instrument
plays both hands; a quieter unison layer is detuned by `detune` cents. `blend`
sets that layer's volume and `gain` controls the complete instrument. Velocity,
note-off and sustain CC64 are supported. 192 sample voices are preallocated.

From the repository root:

```sh
python plugins/piano/fetch.py
make -C plugins/piano TIBIA=/absolute/path/to/tibia
make -C plugins/piano test TIBIA=/absolute/path/to/tibia
```

Python downloads the pinned [TinySoundFont](https://github.com/schellingb/TinySoundFont)
header and MIT license, plus Nando Florestan's
[000 Florestan Piano](https://dev.nando.audio/pages/soundfonts.html), into ignored
`.deps/`. Checksums verify the header and SoundFont archive. The font is a separate
asset from the MIT-licensed synthesizer; it is downloaded from its author's site,
not vendored or relicensed as project code.

Make accepts `TSF=/path/to/header/directory` and `SOUNDFONT=/path/to/piano.sf2`.
The font's first preset is used. `piano.sf2` is copied into the generated Perone
bundle: move the **whole bundle**, including that file, when moving the instrument.
Missing assets fail initialization. File loading happens during preparation.

This optional plugin uses native bundle file access and is deliberately outside
the default plugin build and web catalog. It has been tested on Termux aarch64;
a browser version would need a different way of supplying the sample data.
