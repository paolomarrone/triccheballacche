import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {createHost} from "../web/host.js";

const host = await createHost(), perone = host.perone;
const path = "build/effect.perone/wasm32/fixture.wasm";
await perone.add(path, await readFile(path));
const config = {path, sampleRate: 48000, capacity: 128, inputChannels: 1, outputChannels: 1,
    midiBus: -1, parameters: [0, .5, 0], outputMask: [1, 0], toUi: 16, toDsp: 16};
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
