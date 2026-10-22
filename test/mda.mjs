import assert from "node:assert/strict";
import {readFile, readdir} from "node:fs/promises";
import {execFileSync} from "node:child_process";
import {Perone} from "../web/perone.js";
import {createHost, addFile, renderScore} from "../web/host.js";

const bundles = (await readdir("build/library/mda")).filter(n => n.endsWith(".perone")).sort().map(n => `build/library/mda/${n}`);
assert.equal(bundles.length, 39);
const memory = new ArrayBuffer(16384), host = {FS: {cwd: () => "/"}, HEAPU8: new Uint8Array(memory), HEAPU32: new Uint32Array(memory), HEAPF32: new Float32Array(memory)};
host.HEAPU32.set([1024, 3072], 16 / 4); host.HEAPU32.set([5120, 7168], 32 / 4);
const perone = new Perone(host);
for (const bundle of bundles) {
    const {product} = JSON.parse(await readFile(`${bundle}/product.json`, "utf8"));
    const path = `${bundle}/wasm32/${product.bundleName}.wasm`;
    await perone.add(path, await readFile(path)); // Also rejects any WASI or browser imports.
    for (const sampleRate of [44100, 48000, 96000]) {
        const midiBus = product.buses.findIndex(b => b.type === "midi" && b.direction === "input");
        const inputChannels = product.buses.some(b => b.type === "audio" && b.direction === "input") ? 2 : 0;
        const id = perone.open({path, sampleRate, inputChannels, outputChannels: 2, midiBus, capacity: 257,
            parameters: product.parameters.map(p => p.defaultValue), outputParameters: product.parameters.map(p => p.direction === "output")});
        try {
            perone.control("watch", id, 1);
            if (product.bundleName === "mda_testtone") perone.set(id, 0, .6); // Default is deliberately off.
            for (let run = 0; run < 2; run++) {
                if (run) perone.reset(id);
                if (midiBus >= 0) { host.HEAPU8.set([0x90, 60, 100], 64); perone.midi(id, midiBus, 64); }
                let energy = 0;
                for (let block = 0; block < 100; block++) {
                    const frames = [1, 31, 128, 257][block % 4];
                    for (let i = 0; i < frames; i++) {
                        host.HEAPF32[256 + i] = .2 * Math.sin((block * 257 + i) * .06);
                        host.HEAPF32[768 + i] = .2 * Math.sin((block * 257 + i) * .09);
                    }
                    perone.process(id, inputChannels ? 16 : 0, 32, frames);
                    for (const offset of [1280, 1792]) for (const value of host.HEAPF32.subarray(offset, offset + frames)) {
                        assert(Number.isFinite(value), `${bundle}: non-finite audio`); energy += value * value;
                    }
                }
                assert(energy > 0, `${bundle}: silent at ${sampleRate}`);
                if (midiBus >= 0) { host.HEAPU8.set([0x80, 60, 0], 64); perone.midi(id, midiBus, 64); }
                assert(perone.control("controls", id).values.every(Number.isFinite), "Invalid feedback");
                if (product.bundleName === "mda_specmeter") assert(perone.control("controls", id).values[0] > .1, "Meter follows real audio");
            }
            if (product.bundleName === "mda_looplex") {
                // A reset must retain the selected capacity, not fall back to ten seconds.
                perone.set(id, 0, .1); // 29 seconds
                perone.set(id, 2, 1); // Record
                perone.reset(id);
                host.HEAPF32.fill(.1, 256, 256 + 257);
                host.HEAPF32.fill(.1, 768, 768 + 257);
                for (let frames = 0; frames < 11 * sampleRate; frames += 257) perone.process(id, 16, 32, 257);
                assert(Math.abs(host.HEAPF32[1280] - .1) < .001, "Looplex must still be on its first recording pass");
            }
        } finally { perone.close(id); }
    }
}
execFileSync("./build/test/dsp", bundles.filter(b => !b.endsWith("mdaTestTone.perone")), {stdio: "inherit"});
execFileSync("./build/cli", ["examples/mda.janet", "build/test/mda.wav", "48000"], {stdio: "inherit"});
const offline = await createHost();
for (const path of ["lib/music.janet", "lib/pattern.janet", "examples/mda.janet"]) await addFile(offline, path, await readFile(path));
for (const bundle of bundles) {
    const {product} = JSON.parse(await readFile(`${bundle}/product.json`, "utf8"));
    for (const path of [`${bundle}/product.json`, `${bundle}/wasm32/${product.bundleName}.wasm`]) await addFile(offline, path, await readFile(path));
}
const audio = renderScore(offline, "examples/mda.janet", 48000);
assert.equal(audio.length, 33 * 48000 * 2);
assert(audio.every(Number.isFinite));
assert(audio.some(x => Math.abs(x) > .1));
assert.equal(offline.perone.instances.size, 0);
console.log("OK: 39 standalone Wasm DSPs at 44.1/48/96 kHz, MIDI, reset, meters, native loading and complete native/Wasm score renders");
