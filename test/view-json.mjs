import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {spawnSync} from "node:child_process";
import {createHost, addFile} from "../web/host.js";

const host = await createHost({printErr: () => {}});
for (const path of ["lib/pattern.janet", "lib/trace.janet", "test/view-score.janet", "test/trace-helper.janet",
    "build/test/fixture.perone/product.json", "build/test/effect.perone/product.json",
    "build/test/fixture.perone/wasm32/fixture.wasm", "build/test/effect.perone/wasm32/fixture.wasm"])
    await addFile(host, path, readFileSync(path));
const queries = [["score"], ["range", 7, 0, 7, 0, 8, 100], ["range", 7, 0.2, 0.201, 0, 1, 50],
    ["status", 0.32, 1], ["origin", 7, 0, 1], ["range", 6, 0, 1, 0, 1, 100],
    ["range", 7, 0, 1, 0, 1000, 100], ["range", 7, NaN, 1, 0, 1, 100], ["origin", 7, 0, 1e30], ["unknown"],
    ["automation", 7, 0, 1, 0, .4, 32], ["automation", 7, 0, 1, .25, .4, 32], ["automation", 6, 0, 1, 0, .4, 32],
    ["automation", 7, 0, 0, 0, .4, 32], ["automation", 7, 0, 1, NaN, .4, 32], ["automation", 7, 0, 1, 0, .4, 513],
    ["automation", 7, 0, 1, 8, 9, 32], ["automation", 7, 2, 1, 0, 7, 32]];
for (const rate of [44100, 48000]) {
    const native = spawnSync("./build/test/view_json", [String(rate)], {encoding: "utf8"});
    assert.equal(native.status, 0, native.stderr);
    const expected = native.stdout.trim().split("\n").map(JSON.parse);
    for (const transfer of [false, true]) {
        let score = host.ccall(transfer ? "score_describe" : "score_prepare", "number", ["string", "string", "number"],
            ["test/view-score.janet", readFileSync("test/view-score.janet", "utf8"), rate]);
        assert(score);
        if (transfer) {
            const bytes = host._score_pack_web(score), length = host._score_pack_length();
            assert(bytes && length);
            host._web_score_free(score);
            try { score = host._score_import(bytes, length); }
            finally { host._free(bytes); }
            assert(score, "Cannot reconstruct transferred score and projection");
        }
        const view = host._score_take_view(score);
        assert(view);
        assert.equal(host._score_take_view(score), 0, "Projection ownership can only move once");
        host._web_score_free(score);
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
            assert.equal(actual[0].score.tracks[0][0], 0, "Serial effects retain the original note source");
            assert.deepEqual(actual[0].score.nodes[1].inputs, [0]);
            assert(actual[1].lanes[0].density.length === 100 && !actual[1].lanes[0].notes);
            assert(actual[2].lanes[0].notes.some(note => note[3] === 48), "Long notes crossing the viewport remain visible");
            assert(actual[3].frames.some(frame => frame[0] === "test/trace-helper.janet") && actual[4].frames.length);
            assert(actual[5].stale && actual[6].error && actual[7].error && actual[9].error);
            assert.deepEqual(actual[0].score.nodes[0].automation, [[1, 2]]);
            assert.deepEqual(actual[0].score.nodes[1].automation, []);
            const points = actual[10].points;
            assert.equal(points.length, 3);
            assert.equal(points[0][0], 0);
            assert(Math.abs(points[0][1] - .001) < 1e-9);
            assert.equal(points[0][2], -1);
            assert.equal(points[1][0], .2);
            assert(Math.abs(points[1][1] - .002) < 1e-9);
            assert.deepEqual(actual[11].points, [[.25, points[1][1], points[1][2]], points[2]]);
            assert(actual[12].stale && actual[13].error && actual[14].error && actual[15].error);
            assert.deepEqual(actual[16].points, []);
            assert.equal(actual[17].bins.length, 32);
            assert(actual[17].bins.some(([low, high, last]) => low === 0 && high === 1 && last === 0));
        } finally { host._view_free(view); }
    }
}
console.log("OK: identical native/Wasm graph, notes, automation, origins, validation and stale queries; transferred projections are rebuilt and survive score/DSP teardown");
