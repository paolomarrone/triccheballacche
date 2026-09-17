# Example plugins

Plugins are generated with Tibia and built separately from the host. They require
a C compiler, make, Node.js and `dot` (for example, run `npm install dot` in Tibia).
The generator must provide `perone` and `perone-make`; Brickworks must use the
current plugin API, where `plugin_init` returns `int`.

The default checkouts are `../tibia` and `../brickworks`, relative to the host root.
No DSP dependency is downloaded or modified. From the host root:

```sh
make -C plugins/echo
make -C plugins
make -C plugins TIBIA=/path/to/tibia BRICKWORKS=/path/to/brickworks
make -C plugins synth_mono shape echo drums PERONE_PLATFORM=wasm32
make -C plugins clean
```

Use `make -C plugins PERONE_PLATFORM=wasm32` for all local Wasm plugins.
Emscripten supplies libc/libm; `EMCC` and `EMXX` select its compilers. Use absolute
paths when they are outside `PATH`, since make enters generated build directories.
Wasm modules are standalone, without JavaScript or WASI imports. Their initial
memory is 1 MiB and may grow during preparation. Native binaries already in the
bundle are preserved.

| Project | DSP source | Controls |
| --- | --- | --- |
| `synth_mono` | Original Brickworks `examples/synth_mono/src/plugin.h` | 38 inputs and the `level` output meter. |
| `fx_svf` | Original Brickworks `examples/fx_svf/src/plugin.h` | State-variable filter. |
| `drums` | Local `plugin.h` | Percussion gain and seed. |
| `shape` | Local `plugin.h` | Waveshaper drive/level and DC/low-pass filters. |
| `echo` | Local `plugin.h` | Three delay taps in milliseconds, individual levels and dry signal. |
| `tibia_test` | Local `plugin.h` | Gain, filter, delay, bypass and output. |

`drums` has 32 voices; MIDI notes 0–6 select kick, snare, hat, open hat, crash,
high tom and low tom. Velocity scales each hit, `:gain` scales the instance and
`:seed` affects subsequent hits. Sounds decay naturally, ignore note-off and
steal the oldest voice when full.

Each project supplies `plugin.h` and `product.json`, locally or through Brickworks.
`plugin.mk` generates the wrapper, API and Makefile in `build/gen`, producing
`build/plugin.perone/product.json` and `<platform>/<bundleName>.so` or
`wasm32/<bundleName>.wasm` inside that bundle. The generated build tracks headers.
JSON supplies defaults and metadata; musical presets remain in Janet scores.
Plugins require no Janet, miniaudio or host sources.

Pass the complete `.perone` directory to `build/tools/perone-host` or `daw/plugin`; JSON must
travel with the binary. The host uses the unchanged Perone ABI from Tibia, with
no DSP patches or pitch-bend compensation. See the [host contract and UI support](../README.md#perone-and-plugin-uis).

The original examples in `../brickworks/build/perone` can also be loaded directly.
`examples/brickworks.janet` demonstrates this; no local plugin project or Janet
wrapper is needed per module. The default web catalog includes the 40 C examples;
equivalent C++ variants remain supported by the host but are not published.

From the host root, `make test-plugins` checks the six local prebuilt bundles,
including synth pitch bend at 440/220/880/440 Hz and block-size independence.
`make test-brickworks` checks all bundles in `BRICKWORKS_PERONE` (default
`../brickworks/build/perone`). Both commands test existing binaries without building them.
