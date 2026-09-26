// Use the production standalone Perone loader; no host Wasm or filesystem is needed.
import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {Perone} from "../../web/perone.js";

const host = new Perone({FS: {cwd: () => "/"}});
const path = "/piano.perone/wasm32/piano.wasm";
await host.add(path, await readFile(new URL("build/plugin.perone/wasm32/piano.wasm", import.meta.url)));
const bytes = await readFile(new URL("build/reference.f32", import.meta.url));
const reference = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
const id = host.open({path, sampleRate: 32000, inputChannels: 0, outputChannels: 2, midiBus: 0,
    parameters: [.6, 11, .32], outputMask: [0, 0], capacity: 512});
const p = host.instances.get(id), memory = p.wasm.memory.buffer;
const midi = (...message) => { p.midi.set(message); p.api.midi_msg_in(p.instance, 0, p.midiPointer); };
let maximum = 0, energy = 0;
try {
    for (let block = 0; block < 256; ++block) {
        switch (block) {
        case 0: midi(0x90, 96, 100); midi(0x90, 48, 72); break;
        case 16: midi(0x90, 60, 112); break;
        case 32: midi(0xb0, 64, 127); break;
        case 48: midi(0x80, 96, 0); midi(0x80, 48, 0); midi(0x80, 60, 0); break;
        case 64: p.api.set_parameter(p.instance, 2, .7); p.api.set_parameter(p.instance, 1, 23); break;
        case 96: midi(0xb0, 64, 0); break;
        case 192: midi(0x90, 36, 120); midi(0x90, 64, 100); break;
        case 208: midi(0x90, 64, 80); break;
        case 224: midi(0xb0, 123, 0); break;
        }
        p.api.process(p.instance, p.inputs, p.outputs, 512);
        assert.equal(p.wasm.memory.buffer, memory, "DSP memory must not grow during playback");
        for (let i = 0; i < 512; ++i) for (let channel = 0; channel < 2; ++channel) {
            const value = p.y[channel][i];
            assert(Number.isFinite(value));
            const expected = reference.getFloat32(((block * 512 + i) * 2 + channel) * 4, true);
            maximum = Math.max(maximum, Math.abs(value - expected));
            energy += value * value;
        }
    }
    assert(energy > 1, "The comparison must contain audible audio");
    assert(maximum < 2e-6, `Native/Wasm PCM difference: ${maximum}`);
    p.api.reset(p.instance);
    p.api.process(p.instance, p.inputs, p.outputs, 512);
    assert(p.y.every(channel => channel.every(value => value === 0)));
    console.log(`OK: native/Wasm piano PCM, embedded samples, reset and fixed memory (max error ${maximum})`);
} finally {
    host.closeAll();
    assert.equal(host.instances.size, 0);
}
