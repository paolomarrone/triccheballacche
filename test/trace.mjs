import assert from "node:assert/strict";
import fs from "node:fs";
import {createHost, addFile, renderScore} from "../web/host.js";
import {traceScore, readTrace, activeLines} from "./trace-host.mjs";

const diagnostics = [];
const host = await createHost({printErr: message => diagnostics.push(String(message))});
for (const path of ["lib/trace.janet", "test/trace-score.janet", "test/trace-helper.janet", "lib/pattern.janet",
    "test/schedule.janet", "test/playback.janet", "build/fixture.perone/product.json",
    "build/effect.perone/product.json", "build/fixture.perone/wasm32/fixture.wasm",
    "build/effect.perone/wasm32/fixture.wasm"])
    await addFile(host, path, fs.readFileSync(path));

const path = "test/trace-score.janet", source = fs.readFileSync(path, "utf8");
// Importing the module must leave ordinary scores untouched, also after traced preparations.
const inactive = "test/trace-inactive.janet";
await addFile(host, inactive, new TextEncoder().encode(`
(def push array/push)
(import ../lib/trace)
(assert (= push array/push))
(assert (nil? (root-env :trace/push)))
(dofile "${path}" :env (curenv))
`));
const line = text => {
    const index = source.split("\n").findIndex(row => row.includes(text));
    assert(index >= 0, text);
    return index + 1;
};
const rows = ["first origin", "equal value, distinct origin", "helper origin", "surviving tail call site",
    "controls origin", "direct note", "direct parameter", "# fallback"].map(line);
let reference;
for (const rate of [44100, 48000]) {
    const plain = renderScore(host, path, rate);
    assert.deepEqual(renderScore(host, inactive, rate), plain, "Import enabled tracing implicitly");
    const entry = await traceScore(host, path, source);
    const traced = renderScore(host, entry, rate);
    assert.deepEqual(traced, plain, `Tracing changed PCM at ${rate} Hz`);
    assert.equal(host.perone.instances.size, 0);
    const trace = readTrace(host);
    fs.writeFileSync("build/trace-fixture.json", JSON.stringify(trace, null, 2));
    assert.equal(trace.events.length, 17); // Also includes three events collected inside reusable helpers.
    assert.equal(trace["fallback-events"], 1);
    assert.equal(trace["pushed-events"], 3);
    const frames = trace.events.map(event => event[2].flatMap(id => trace.locations[id]));
    for (const expected of rows) assert(frames.some(stack => stack.some(frame => frame.line === expected)), expected);
    assert(!frames.some(stack => stack.some(frame => frame.line === line("eliminated helper frame"))));
    assert(frames.some(stack => stack.some(frame => frame.line === line("ordinary call site"))));
    const first = trace.events[0][2], second = trace.events[2][2];
    assert.equal(first.length, 2, "Equal patterns must report their ambiguous origin");
    assert.deepEqual(first, second);
    assert.deepEqual(trace.events[4][2], first, "Reverse must retain the possible origins");
    assert.deepEqual(trace.events[5][2], first);
    assert.equal(trace.events[6][0], 1.125); // Serial + stretch + conversion at 120 BPM.
    assert.equal(trace.events[6][1], 1.625);
    assert.equal(trace.events[7][1], 1.875);
    assert(activeLines(trace, path, 0.01).has(line("first origin")));
    assert(!activeLines(trace, path, 0.2).has(line("first origin"))); // A rest stays unhighlighted.
    assert(activeLines(trace, path, 2.1).has(line("direct parameter")));
    assert(!activeLines(trace, path, 2.3).has(line("direct parameter")));
    for (const [index, call] of [[14, "first producer call"], [15, "second producer call"]]) {
        const stack = frames[index];
        assert(stack.some(frame => frame.line === line("# event producer")), "Lost the producing function's tail call");
        assert(stack.some(frame => frame.line === line(call)), "Lost the caller inside the phrase");
        assert(!stack.some(frame => frame.line === line("collection happens later")));
    }
    assert(frames[16].some(frame => frame.file === "test/trace-helper.janet" && frame.line === 3));
    assert(frames[16].some(frame => frame.line === line("imported producer call")));
    assert(activeLines(trace, path, 2.65).has(line("# event producer")), "Short notes need a visible pulse");
    const stable = {locations: trace.locations, events: trace.events};
    if (reference) assert.deepEqual(stable, reference, "Locations changed on the next preparation");
    reference = stable;
    console.log(`OK: trace preserves PCM at ${rate} Hz, ambiguous origins, transformations, direct calls and tail-call limits`);
}

// Existing validation scores still work with instrumented functions.
for (const path of ["test/schedule.janet", "test/playback.janet"]) {
    const plain = renderScore(host, path, 48000);
    const entry = await traceScore(host, path, fs.readFileSync(path, "utf8"));
    assert.deepEqual(renderScore(host, entry, 48000), plain);
}

// Failure, including a second installation, must not leave a report or poison ordinary playback.
for (const failure of ['(error "trace failure")', '(trace/install (curenv) (dyn :current-file))']) {
    diagnostics.length = 0;
    const bad = await traceScore(host, path, source + "\n" + failure);
    assert.throws(() => renderScore(host, bad, 48000));
    assert(diagnostics.some(text => text.includes(failure.startsWith("(error")
        ? "trace failure" : "source tracing is already installed")));
    assert.throws(() => readTrace(host));
    assert.equal(host.perone.instances.size, 0);
    await addFile(host, path, new TextEncoder().encode(source));
    assert.deepEqual(renderScore(host, inactive, 48000), renderScore(host, path, 48000));
}
renderScore(host, await traceScore(host, path, source), 48000);
assert.deepEqual(readTrace(host).events, reference.events);
console.log("OK: tracing is opt-in, installation is unique, and failed preparations permit ordinary and traced reuse");
