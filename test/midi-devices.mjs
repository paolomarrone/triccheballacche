import assert from "node:assert/strict";
import {midiDevices} from "../web/midi.js";

function port(type, id) {
    return {
        type, id, name: id, state: "connected", closes: 0,
        async open() { if (this.failOpen) throw Error("Open failed"); },
        async close() { ++this.closes; if (this.failClose) throw Error("Close failed"); },
        clear() {}, send() {}
    };
}
const input = port("input", "keyboard"), first = port("output", "first"), second = port("output", "second");
const access = {inputs: new Map([[input.id, input]]), outputs: new Map([[first.id, first], [second.id, second]])};
Object.defineProperty(globalThis, "navigator", {value: {requestMIDIAccess: async () => access}, configurable: true});
const captured = new Set(), messages = [], failures = [];
const player = {
    active: true,
    midi(node, data) { messages.push({node, data}); },
    captureMidi(node, enabled) { enabled ? captured.add(node) : captured.delete(node); },
    readMidi() { return []; }
};
const devices = midiDevices(() => player, error => failures.push(error));
try {
    await devices.connect("input:keyboard", 0);
    await devices.connect("output:first", 0);
    await devices.connect("output:second", 0);
    input.failOpen = true;
    await assert.rejects(devices.connect("input:keyboard", 1), /Open failed/);
    devices.start();
    input.onmidimessage({data: [0x90, 60, 100]});
    assert.equal(messages.at(-1).node, 0, "Failed connection keeps the original input destination");
    devices.stop();
    input.onmidimessage({data: [0x90, 61, 100]});
    assert.equal(messages.length, 1, "Stopped devices cannot enqueue notes");

    first.failClose = true;
    await assert.rejects(devices.connect("output:first", -1), /Close failed/);
    assert.equal((await devices.list()).devices.find(p => p.id === "output:first").node, 0);
    assert(captured.has(0), "Failed close retains its subscription");
    first.failClose = false;
    await devices.connect("output:first", -1);
    assert(captured.has(0), "Another device still consumes this node");

    second.failClose = true;
    await assert.rejects(devices.detach(), /Close failed/);
    assert.equal(captured.size, 0, "Detach removes subscriptions even if a port refuses to close");
    assert.equal(input.onmidimessage, null);
    assert.equal(input.closes, 1);
    assert((await devices.list()).devices.every(p => p.node === -1));
    assert.deepEqual(failures, []);
    console.log("OK: Web MIDI connection rollback, shared outputs, stopped input and failed device cleanup");
} finally {
    second.failClose = false;
    await devices.detach();
}
