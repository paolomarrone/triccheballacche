import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, writeFile, rm} from "node:fs/promises";
import {graphLayout} from "../editor/graph.js";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

// Shared branches, forward references, repeated inputs and both extreme DAG shapes.
for (const inputs of [
    [[2], [], [1], [2], [0], [3, 4, 3]],
    Array.from({length: 128}, (_, i) => i < 127 ? [] : Array.from({length: 127}, (_, j) => j)),
    Array.from({length: 128}, (_, i) => i < 127 ? [i + 1] : [])
]) {
    const {nodes, width, height} = graphLayout({nodes: inputs.map(inputs => ({inputs})), tracks: [], output: 0});
    assert.equal(nodes.size, inputs.length);
    for (const node of nodes.values()) {
        assert(Number.isFinite(node.x) && Number.isFinite(node.y));
        assert(node.x >= 0 && node.y >= 0 && node.x + node.width <= width && node.y + node.height <= height);
        assert.deepEqual(node.inputs, inputs[node.id]);
        for (const id of node.inputs) assert(nodes.get(id).x + nodes.get(id).width < node.x);
        for (const other of nodes.values())
            assert(node.id === other.id || node.x !== other.x || node.y + node.height <= other.y || other.y + other.height <= node.y);
    }
}

// Only track/master stages disappear, including nested groups. Explicit one-input
// mixes, repeated inputs and taps before/after a track keep their connections.
const score = {
    nodes: [[], [0], [1], [1], [2], [3, 4, 3, 0], [5], [6]].map(inputs => ({inputs})),
    tracks: [[0, 1], [0, 4, 2], [5, 6], [-1, 7]], output: 7
};
const original = structuredClone(score), compact = graphLayout(score);
assert.deepEqual([...compact.nodes.keys()], [0, 2, 3, 5]);
assert.deepEqual([...compact.nodes.values()].map(n => n.inputs), [[], [0], [0], [3, 2, 3, 0]]);
assert.deepEqual([...compact.nodes.values()].map(n => n.tracks), [[1], [4], [], [6, 7]]);
assert.equal(compact.output, 5);
assert.deepEqual(score, original, "Visual folding does not change the prepared audio graph");

const directory = await mkdtemp("build/test/graph-"), entry = `${directory}/score.janet`;
const source = `(import ../../../lib/pattern :as p)
(def effect (daw/plugin :effect "build/test/effect.perone" {:gain 0.5}))
(def synth (daw/plugin :synth "build/test/fixture.perone" {:gain 0.001}))
(def source (daw/track synth {:name "Source <&>"}))
(daw/through source effect)
(def dry (daw/mix [source] {:id :dry}))
(def wet (daw/track effect {:name "Parallel"}))
(daw/output (daw/master (daw/mix [dry wet dry] {:id :output})))
(daw/tempo 60)
(daw/score (p/loop (p/events 1 [[0 0.5 [:note synth 60 80]]])) {:quantum 0.25})
`;
let app, server;
try {
    await writeFile(entry, source);
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", entry, "build/test/fixture.perone", "build/test/effect.perone"]);
    server = serve(0); await once(server, "listening");
    for (const mode of ["native", "web"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") { app = nativeEditor(entry); url = await app.url; }
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            await call("Emulation.setDeviceMetricsOverride", {width: 1400, height: 900, deviceScaleFactor: 1, mobile: false});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#play")?.disabled === false');
            await click("#graph-tab");
            assert(await evaluate('document.querySelector("#graph-fit").disabled'));
            assert.equal(await evaluate('document.querySelector(".graph-message").textContent'), "Press Play to see the graph");
            await key("ArrowLeft");
            assert(await evaluate('document.querySelector("#graph-view").hidden'));
            await key("ArrowRight");
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > 0');
            await wait('document.querySelectorAll(".graph-node").length === 4');
            await evaluate(`(() => {
                const split = document.querySelector('#split');
                for (let i = 0; i < 10; ++i) split.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowUp'}));
            })()`);
            await click("#graph-fit");
            const edges = () => evaluate('Array.from(document.querySelectorAll(".graph-cable"), p => [Number(p.dataset.from), Number(p.dataset.to)]).sort()');
            const expected = [[1, 0], [1, 3], [3, 5], [0, 5], [3, 5]].sort();
            assert.deepEqual(await edges(), expected, "Track stages fold into cables, preserving repeated inputs");
            assert.equal(await evaluate(`document.querySelector('.graph-node[data-node="1"] small').textContent`), "Instrument · Source <&>");
            assert.equal(await evaluate(`document.querySelector('.graph-node[data-node="5"] small').textContent`), "Mix · Output");
            assert.equal(await evaluate('document.querySelectorAll(".graph-node.mix").length'), 2, "Explicit mixes remain visible, even with one input");
            assert(await evaluate(`document.querySelector('.graph-node[data-node="0"] .graph-port').title.endsWith(' → Source <&>')`));
            assert.equal(await evaluate('document.querySelectorAll(".graph-node.effect").length'), 1, "A shared effect is never duplicated");
            assert(await evaluate(`(() => {
                const r = document.querySelector('#graph-view').getBoundingClientRect();
                return [...document.querySelectorAll('.graph-node')].every(node => {
                    const n = node.getBoundingClientRect();
                    return n.left >= r.left && n.right <= r.right && n.top >= r.top && n.bottom <= r.bottom;
                });
            })()`), "Fit includes the complete graph");
            await click('.graph-node[data-node="0"]');
            assert.equal(await evaluate('document.querySelectorAll(".graph-cable.highlighted").length'), 2);
            assert.equal(await evaluate('document.querySelector(".graph-node.dim").dataset.node'), "3", "A parallel dry branch is outside the effect's path");
            const transform = () => evaluate('document.querySelector(".graph-world").style.transform');
            const fitted = await transform();
            await evaluate(`(() => {
                const p = document.querySelector('#graph-view'), r = p.getBoundingClientRect();
                p.dispatchEvent(new WheelEvent('wheel', {bubbles: true, cancelable: true, deltaY: -200,
                    clientX: r.left + r.width / 2, clientY: r.top + r.height / 2}));
            })()`);
            assert.notEqual(await transform(), fitted);
            const point = await evaluate('(() => { const r = document.querySelector("#graph-view").getBoundingClientRect(); return {x: r.left + 8, y: r.top + 8}; })()');
            const zoomed = await transform();
            await call("Input.dispatchMouseEvent", {type: "mousePressed", ...point, button: "left", clickCount: 1});
            await call("Input.dispatchMouseEvent", {type: "mouseMoved", x: point.x + 80, y: point.y + 30, button: "left", buttons: 1});
            await call("Input.dispatchMouseEvent", {type: "mouseReleased", x: point.x + 80, y: point.y + 30, button: "left", clickCount: 1});
            assert.notEqual(await transform(), zoomed);
            const moved = await transform();
            await click("#score-tab");
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            await click("#graph-tab");
            await evaluate('new Promise(requestAnimationFrame)');
            assert.equal(await transform(), moved, "Switching projections preserves graph navigation");
            assert.equal(await evaluate('document.querySelector(".graph-node[aria-pressed=true]").dataset.node'), "0");
            assert.deepEqual(await edges(), expected);
            const revision = await evaluate('Number(document.querySelector("#graph-view").dataset.revision)');
            await set("#code", source.replace(":gain 0.001", ":gain 0.002"));
            await click("#play");
            await wait(`Number(document.querySelector('#graph-view').dataset.revision) > ${revision}`);
            assert.equal(await transform(), moved, "Live updates preserve navigation");
            assert.equal(await evaluate('document.querySelector(".graph-node[aria-pressed=true]").dataset.node'), "0");
            await click("#graph-fit");
            await call("Emulation.setEmulatedMedia", {features: [{name: "prefers-color-scheme", value: "dark"}]});
            const screenshot = await call("Page.captureScreenshot", {format: "png"});
            await writeFile(`build/test/graph-${mode}.png`, Buffer.from(screenshot.data, "base64"));
            await set("#code", '(error "keep the graph")');
            await click("#play");
            await wait('!document.querySelector("#errors").hidden && !document.querySelector("#play").disabled');
            assert.deepEqual(await edges(), expected, "Failed preparation preserves the last graph");
            await click("#stop");
            await set("#code", source.replace("[dry wet dry]", "[dry wet]"));
            await click("#play");
            await wait('document.querySelectorAll(".graph-cable").length === 4');
            assert.equal(await evaluate('document.querySelectorAll(".graph-node[aria-pressed=true]").length'), 0);
            assert.equal(await evaluate('document.querySelectorAll(".graph-node").length'), 4);
            assert(await evaluate('document.querySelector("#errors").hidden'));
            await click("#stop");
            assert.deepEqual(diagnostics, []);
            console.log(`OK: ${mode} routing graph, shared branches, repeated inputs, navigation, live revisions and errors`);
        }, {graphics: true});
        await app?.close(); app = undefined;
    }
} finally {
    try { await app?.close(); }
    finally {
        if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
        await rm(directory, {recursive: true, force: true});
    }
}
