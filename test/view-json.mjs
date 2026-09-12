import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {spawnSync} from "node:child_process";
import {createHost, addFile} from "../web/host.js";

const host = await createHost({printErr: () => {}});
for (const path of ["lib/pattern.janet", "lib/trace.janet", "test/view-score.janet", "test/trace-helper.janet",
    "build/fixture.perone/product.json", "build/effect.perone/product.json",
    "build/fixture.perone/wasm32/fixture.wasm", "build/effect.perone/wasm32/fixture.wasm"])
    await addFile(host, path, readFileSync(path));
const queries = [["score"], ["range", 7, 0, 7, 0, 8, 100], ["range", 7, 0.2, 0.201, 0, 1, 50],
    ["status", 0.32, 1], ["note", 7, 0, 1], ["range", 6, 0, 1, 0, 1, 100],
    ["range", 7, 0, 1, 0, 1000, 100], ["range", 7, NaN, 1, 0, 1, 100], ["note", 7, 0, 1e30], ["unknown"]];
for (const rate of [44100, 48000]) {
    const native = spawnSync("./build/view_json_test", [String(rate)], {encoding: "utf8"});
    assert.equal(native.status, 0, native.stderr);
    const expected = native.stdout.trim().split("\n").map(JSON.parse);
    const score = host.ccall("score_prepare", "number", ["string", "string", "number"],
        ["test/view-score.janet", readFileSync("test/view-score.janet", "utf8"), rate]);
    assert(score);
    const view = host._score_take_view(score);
    assert(view);
    assert.equal(host._score_take_view(score), 0, "Projection ownership can only move once");
    host._score_free(score);
    assert.equal(host.perone.instances.size, 0);
    try {
        const actual = queries.map(([op, ...values]) => {
            const args = Array.from({length: 6}, (_, i) => values[i] ?? 0);
            const pointer = host.ccall("score_view_json", "number",
                ["number", "number", "string", ...args.map(() => "number")], [view, 7, op, ...args]);
            assert(pointer);
            try { return JSON.parse(host.UTF8ToString(pointer)); }
            finally { host._free(pointer); }
        });
        assert.deepEqual(actual, expected, `Native/Wasm query mismatch at ${rate}`);
        assert(actual[1].lanes[0].density.length === 100 && !actual[1].lanes[0].notes);
        assert(actual[2].lanes[0].notes.some(note => note[3] === 48), "Long notes crossing the viewport remain visible");
        assert(actual[3].frames.some(frame => frame[0] === "test/trace-helper.janet") && actual[4].frames.length);
        assert(actual[5].stale && actual[6].error && actual[7].error && actual[9].error);
    } finally { host._view_free(view); }
}
console.log("OK: identical native/Wasm graph, density, notes, origins, validation and stale queries; projection survives DSP teardown");
