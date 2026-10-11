import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {createHost} from "../web/host.js";

const host = await createHost(), perone = host.perone;
const path = "build/test/effect.perone/wasm32/fixture.wasm";
await perone.add(path, await readFile(path));
const config = {path, sampleRate: 48000, capacity: 128, inputChannels: 1, outputChannels: 1,
    midiBus: -1, parameters: [0, .5, 0], outputParameters: [1, 0, 0], toUi: 16, toDsp: 16};
const left = perone.open(config), right = perone.open(config);
const a = perone.instances.get(left), b = perone.instances.get(right);
const process = p => { p.x[0].fill(.25); p.api.process(p.instance, p.inputs, p.outputs, 1); };
try {
    perone.control("watch", left, true);
    perone.control("watch", right, true);
    perone.control("parameter", left, 1, .2);
    perone.control("parameter", left, 1, .3);
    assert.equal(perone.control("controls", left).values[1], null);
    perone.control("message", left, [0, 127, 255]);
    perone.sync(left, right);
    process(a); process(b);
    for (const id of [left, right]) {
        const data = perone.control("controls", id);
        assert(Math.abs(data.values[0] - .3) < 1e-7);
        assert.deepEqual(data.messages, [[0, 127, 255]]);
        assert(Math.abs(perone.instances.get(id).y[0][0] - .075) < 1e-7, "Both audio channels use the edit");
    }
    // Score automation remains authoritative after the control boundary.
    perone.set(left, 1, .1);
    assert(Math.abs(perone.control("controls", left).values[1] - .1) < 1e-7);
    assert.throws(() => perone.control("parameter", left, 0, .5), /Invalid/);
    assert.throws(() => perone.control("parameter", left, 1, Infinity), /Invalid/);
    assert.throws(() => perone.control("message", left, [256]), /Invalid/);
    assert.throws(() => perone.control("message", left, Array(17).fill(0)), /too large/);
    const received = [], input = a.api.msg_in;
    a.api.msg_in = (_, size, pointer) => received.push(Array.from(new Uint8Array(a.wasm.memory.buffer, pointer, size)));
    for (let i = 0; i < 64; ++i) perone.control("message", left, [i]);
    assert.throws(() => perone.control("message", left, []), /queue full/);
    // Detaching notifications must not strand already accepted edits/messages.
    perone.control("watch", left, false);
    perone.sync(left, 0);
    assert.deepEqual(received, Array.from({length: 64}, (_, i) => [i]));
    a.api.msg_in = input;
    perone.control("watch", left, true);
    for (let i = 0; i < 65; ++i) {
        perone.control("message", left, []);
        perone.sync(left, 0); process(a);
    }
    assert.throws(() => perone.control("controls", left), /overflow/);
    perone.control("watch", left, false);
    assert.throws(() => perone.control("parameter", left, 1, .5), /not attached/);
    perone.control("watch", left, true);
    assert.deepEqual(perone.control("controls", left).messages, []);
} finally { perone.closeAll(); }
console.log("OK: Wasm control acknowledgement, actual stereo edits, automation, binary messages, FIFO bounds, detach and overflow recovery");

for (const effect of [false, true]) {
    const path = `build/test/${effect ? 'effect' : 'fixture'}.perone/wasm32/fixture.wasm`;
    await perone.add(path, await readFile(path));
    const id = perone.open({...config, path, outputChannels: effect ? 1 : 2, inputChannels: effect ? 1 : 0,
        midiBus: effect ? -1 : 0, minimum: [0, 0, 0], maximum: [1, 1, effect ? 1 : 3], flags: [1, 0, effect ? 0 : 2]});
    const p = perone.instances.get(id);
    try {
        perone.control("watch", id, true);
        perone.control("parameter", id, 1, .625);
        const state = perone.state(id);
        // Same envelope and little-endian parameter values as the native fixtures.
        const expected = new Uint8Array(effect ? 20 : 16), view = new DataView(expected.buffer);
        view.setUint32(0, 0x31524550, true); view.setUint32(4, effect ? 0 : 1, true);
        view.setFloat32(effect ? 12 : 8, .625, true);
        assert.deepEqual(state, expected);
        perone.set(id, 1, .125);
        perone.state(id, state);
        assert.equal(perone.control("controls", id).values[1], .625);
        assert.throws(() => perone.state(id, state.subarray(0, state.length - 1)));
        new DataView(state.buffer).setFloat32(effect ? 12 : 8, NaN, true);
        assert.throws(() => perone.state(id, state));
        assert.equal(perone.control("controls", id).values[1], .625);
        if (!effect) {
            const save = p.api.state_save;
            p.api.state_save = (...args) => { p.wasm.memory.grow(1); return save(...args); };
            const memory = p.memory;
            perone.state(id);
            assert.notEqual(p.memory, memory, "State allocation may grow memory; audio views must be refreshed");
            perone.set(id, 1, .5); perone.set(id, 2, 2);
            perone.transport(id, 1, 180, 2.5);
            p.api.process(p.instance, 0, p.outputs, 1);
            assert.equal(p.y[0][0], 2.5);
            perone.transport(id, 0, 180, 2.5);
            p.api.process(p.instance, 0, p.outputs, 1);
            assert.equal(p.y[0][0], 2);
        }
    } finally { perone.close(id); }
}
console.log("OK: Wasm custom/parameter presets match native envelopes, reject invalid data, refresh grown memory and deliver transport");
