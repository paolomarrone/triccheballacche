import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {createHost, addFile, renderScore} from "../web/host.js";

const host = await createHost({printErr: () => {}});
for (const path of ["lib/pattern.janet", "build/test/midi.perone/product.json", "build/test/midi.perone/wasm32/midi.wasm",
    "build/test/fixture.perone/wasm32/fixture.wasm"])
    await addFile(host, path, await readFile(path));
const source = fault => `(def m (daw/plugin "build/test/midi.perone" {:fault ${fault}}))
    (daw/midi-output m) (daw/note m 0.001 0.003 60 100)
    (daw/midi-event m 0.002 [0xc2 12]) (daw/end 0.01)`;
async function render(text) {
    await addFile(host, "test/midi-only.janet", new TextEncoder().encode(text));
    return renderScore(host, "test/midi-only.janet", 48000);
}
for (let fault = 1; fault <= 4; ++fault) {
    await assert.rejects(render(source(fault)), /Audio rendering failed/);
    assert.equal(host.perone.instances.size, 0, "Failed MIDI processing leaked an instance");
    assert((await render(source(0))).every(value => value === 0));
    assert.equal(host.perone.instances.size, 0);
}

// An ABI 2-only binary must not be accepted for a product declaring MIDI output.
const binary = host.perone.path("build/test/midi.perone/wasm32/midi.wasm");
const module = host.perone.modules.get(binary);
host.perone.modules.set(binary, host.perone.modules.get(host.perone.path("build/test/fixture.perone/wasm32/fixture.wasm")));
await assert.rejects(render(source(0)), /Score preparation failed/);
assert.equal(host.perone.instances.size, 0);
host.perone.modules.set(binary, module);

const original = JSON.parse(await readFile("test/perone/midi.json", "utf8"));
for (const buses of [[], [original.product.buses[0], {...original.product.buses[0], id: "another"}],
    [original.product.buses[0], original.product.buses[1], {...original.product.buses[1], id: "another"}]]) {
    await addFile(host, "build/test/midi.perone/product.json", new TextEncoder().encode(
        JSON.stringify({product: {...original.product, buses}})));
    await assert.rejects(render(source(0)), /Score preparation failed/);
    assert.equal(host.perone.instances.size, 0);
}
console.log("OK: Wasm MIDI-only graph, invalid bus/offset/message, overflow recovery, ABI 2 rejection for MIDI outputs and metadata validation");
