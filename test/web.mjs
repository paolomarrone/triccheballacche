import assert from "node:assert/strict";
import fs from "node:fs";
import {spawnSync} from "node:child_process";
import {createHost, addFile, renderScore} from "../web/host.js";
import {Perone} from "../web/perone.js";

const host = await createHost({printErr: () => {}});
for (const path of ["lib/music.janet", "lib/pattern.janet", "test/music.janet", "test/pattern.janet",
    "test/daw.janet", "test/schedule.janet", "test/playback.janet", "build/fixture.perone/product.json", "build/effect.perone/product.json",
    "build/fixture.perone/wasm32/fixture.wasm", "build/effect.perone/wasm32/fixture.wasm"])
    await addFile(host, path, fs.readFileSync(path));

function pcm(file) {
    const bytes = fs.readFileSync(file);
    for (let pos = 12; pos + 8 <= bytes.length;) {
        const size = bytes.readUInt32LE(pos + 4);
        if (bytes.toString("ascii", pos, pos + 4) === "data")
            return new Float32Array(bytes.buffer.slice(bytes.byteOffset + pos + 8, bytes.byteOffset + pos + 8 + size));
        pos += 8 + size + (size & 1);
    }
    throw Error("Missing WAV audio");
}

for (const rate of [44100, 48000]) {
    for (const score of ["test/schedule.janet", "test/playback.janet", "test/daw.janet"]) {
        const file = `build/web/reference-${rate}.wav`;
        try {
            const result = spawnSync("./build/daw", [score, file, String(rate)], {encoding: "utf8"});
            assert.equal(result.status, 0, result.stderr);
            const native = pcm(file), wasm = renderScore(host, score, rate);
            assert.equal(wasm.length, native.length);
            assert.deepEqual(wasm, native, `Native/Wasm PCM differs: ${score}, ${rate}`);
            assert.equal(host.perone.instances.size, 0);
        } finally {
            fs.rmSync(file, {force: true});
        }
    }
}

await addFile(host, "test/bad.janet", new TextEncoder().encode(
    '(daw/plugin "build/fixture.perone") (error "failure after allocation")'));
assert.throws(() => renderScore(host, "test/bad.janet", 48000));
assert.equal(host.perone.instances.size, 0);
await addFile(host, "build/unloaded.perone/product.json", fs.readFileSync("test/perone/product.json"));
await addFile(host, "test/bad.janet", new TextEncoder().encode(
    '(daw/plugin "build/fixture.perone") (daw/plugin "build/unloaded.perone") (daw/end 1)'));
assert.throws(() => renderScore(host, "test/bad.janet", 48000));
assert.equal(host.perone.instances.size, 0);
assert.throws(() => renderScore(host, "test/schedule.janet", 384001));
assert.equal(host.perone.instances.size, 0);
// A subsequent valid score still works after errors.
assert.equal(renderScore(host, "test/schedule.janet", 48000).length, 144000);
console.log("OK: Janet patterns, validation, stereo/mono DSP, event boundaries and 60-second automation match native PCM exactly at 44.1/48 kHz; cleanup and recovery pass");

// Rebuilding initial state preserves C handles and configured defaults, including duplicated mono effects.
const path = "test/playback.janet", expected = renderScore(host, path, 48000);
const score = host.ccall("score_new", "number", ["string", "number"], [path, 48000]);
assert(score);
const source = host.perone, destination = new Perone(host);
try {
    const setup = source.releasePrepared();
    assert.equal(source.instances.size, 0);
    assert.equal(source.remote.size, 3);
    destination.restorePrepared(setup);
    host.perone = destination;
    const actual = new Float32Array(expected.length), pointer = host._score_buffer(score) / 4;
    for (let position = 0; position < actual.length;) {
        const n = host._score_render(score);
        assert(n > 0);
        actual.set(host.HEAPF32.subarray(pointer, pointer + n * 2), position);
        position += n * 2;
    }
    assert.deepEqual(actual, expected);
    assert.throws(() => destination.releasePrepared(), /after rendering/);
    assert.equal(destination.instances.size, 3);
    destination.closeAll();

    const invalid = {...setup, modules: setup.modules.map(([path, module]) => [path, path.includes("effect.perone")
        ? new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0])) : module])};
    assert.throws(() => destination.restorePrepared(invalid));
    assert.equal(destination.instances.size, 0, "Failed setup must roll back earlier DSP allocations");
    destination.restorePrepared(setup);

    let freed = 0;
    for (const p of destination.instances.values()) {
        const free = p.api.free;
        p.api.free = instance => { free(instance); freed++; };
    }
    const first = destination.instances.values().next().value, fini = first.api.fini;
    first.api.fini = instance => { fini(instance); throw Error("injected cleanup failure"); };
    assert.throws(() => destination.closeAll(), /injected cleanup failure/);
    assert.equal(freed, 3, "A failing fini must not skip other DSP instances");
    assert.equal(destination.instances.size, 0);
} finally {
    destination.closeAll();
    host.perone = source;
    host._score_free(score);
}
assert.equal(source.remote.size, 0);
console.log("OK: explicit DSP ownership, initial-state reconstruction, rollback and cleanup after fini failure");
