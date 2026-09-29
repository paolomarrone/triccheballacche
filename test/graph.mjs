import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, writeFile, rm} from "node:fs/promises";
import {graphLayout, graphPath} from "../editor/graph.js";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

// Shared branches, forward references, repeated inputs and both extreme DAG shapes.
for (const inputs of [
    [[2], [], [1], [2], [0], [3, 4, 3]],
    [[], [0], [1], [], [2, 3], [], [5]],
    Array.from({length: 128}, (_, i) => i < 127 ? [] : Array.from({length: 127}, (_, j) => j)),
    Array.from({length: 128}, (_, i) => i < 127 ? [i + 1] : [])
]) {
    const {nodes, width, height} = graphLayout({nodes: inputs.map(inputs => ({inputs})), tracks: [], output: 0});
    assert.equal(nodes.size, inputs.length);
    const left = Math.min(...[...nodes.values()].map(node => node.x));
    for (const node of nodes.values()) {
        if (!node.inputs.length) assert.equal(node.x, left, "All sources align, including short and disconnected chains");
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
assert.deepEqual(graphPath(compact.nodes, 2).ids, [0, 2, 5], "A parallel effect excludes the dry branch");
assert.deepEqual(graphPath(compact.nodes, 5).ids, [0, 2, 3, 5], "Selecting a merge includes its own inputs");
const master = graphLayout({
    nodes: [[3], [0], [], [], [1, 2], [4], [5], [6]].map(inputs => ({inputs})),
    tracks: [[3, 1], [-1, 7]], output: 7
});
assert.deepEqual(graphPath(master.nodes, 0).ids, [3, 0, 4, 5, 6],
    "A track effect follows its source and both master effects, without the other instrument");

// Reversed declarations should not force crossings. Long cables must follow a
// route around intermediate nodes, and parallel inputs must stay distinct.
for (const inputs of [
    [[], [], [], [], [3], [2], [1], [0]],
    [[], [0], [0, 1, 0]],
    [[], [], [1], [0], [0, 2, 3]]
]) {
    const score = {nodes: inputs.map(inputs => ({inputs})), tracks: [], output: inputs.length - 1};
    const graph = graphLayout(score);
    assert.deepEqual(graphLayout(score), graph, "Layout is deterministic");
    const segments = [];
    for (const edge of graph.edges) {
        assert.equal(edge.from, inputs[edge.to][edge.slot]);
        for (let i = 1; i < edge.points.length; ++i) {
            const a = edge.points[i - 1], b = edge.points[i];
            segments.push([a, b]);
            assert(a.x < b.x, "Cables run left to right");
            // Sample the same cubic geometry rendered by the view.
            for (let step = 0; step <= 20; ++step) {
                const t = step / 20, u = 1 - t, mid = (a.x + b.x) / 2;
                const x = u ** 3 * a.x + 3 * u * t * mid + t ** 3 * b.x;
                const y = (u ** 3 + 3 * u * u * t) * a.y + (3 * u * t * t + t ** 3) * b.y;
                for (const node of graph.nodes.values())
                    assert(x <= node.x || x >= node.x + node.width || y <= node.y || y >= node.y + node.height,
                        "Cables avoid node interiors");
            }
        }
    }
    const side = (a, b, c) => (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    for (let i = 0; i < segments.length; ++i) for (let j = i + 1; j < segments.length; ++j) {
        const [a, b] = segments[i], [c, d] = segments[j];
        assert(!(side(a, b, c) * side(a, b, d) < 0 && side(c, d, a) * side(c, d, b) < 0),
            "These planar examples need no crossings");
    }
}

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
            const effectUI = `document.querySelector('.plugin[data-node="0"] .plugin-body > div')?.shadowRoot`;
            await wait(`${effectUI}?.querySelectorAll('.perone-controls label').length === 3`);
            assert(await evaluate('document.querySelector("#views").checked && !document.querySelector("#plugins").hidden'));
            const openNodes = () => evaluate('Array.from(document.querySelectorAll(".plugin[open]"), p => Number(p.dataset.node))');
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin"), p => Number(p.dataset.node))'), [1, 0, 5],
                "The sidebar follows the effect's upstream and downstream paths, excluding the parallel dry branch");
            assert.deepEqual(await openNodes(), [0]);
            const sidebarWidth = () => evaluate('document.querySelector("#plugins").getBoundingClientRect().width');
            assert.equal(await sidebarWidth(), 360);
            assert.deepEqual(await evaluate(`['#graph-tab', '.graph-node', '.plugin summary', '#code', '#path'].map(s => getComputedStyle(document.querySelector(s)).userSelect)`),
                ['none', 'none', 'none', 'text', 'text']);
            await evaluate(`globalThis.retainedGraphUI = ${effectUI}`);
            await click('.graph-node[data-node="0"]');
            assert.deepEqual(await openNodes(), [0]);
            assert(await evaluate(`retainedGraphUI === ${effectUI}`), "Reselecting a node preserves its UI");
            await evaluate(`(() => {
                const gain = ${effectUI}.querySelector('input');
                gain.value = .3; gain.dispatchEvent(new Event('input')); gain.dispatchEvent(new Event('change'));
            })()`);
            await wait(`${effectUI}.querySelectorAll('output')[1].textContent === '0.3'`);
            await evaluate('new Promise(requestAnimationFrame)');
            await click('.graph-node[data-node="3"]');
            await wait(`document.querySelector('.plugin[data-node="3"]').open`);
            assert.deepEqual(await openNodes(), [3]);
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin"), p => Number(p.dataset.node))'), [1, 3, 5],
                "The dry path includes its source and output, without the parallel effect");
            assert.equal(await sidebarWidth(), 360, "Changing nodes does not resize the sidebar");
            await evaluate(`globalThis.retainedGraphEntry = document.querySelector('.plugin[data-node="3"]')`);
            await click('.graph-node[data-node="5"]');
            assert.deepEqual(await openNodes(), [5]);
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin"), p => Number(p.dataset.node))'), [1, 0, 3, 5],
                "Selecting the output merge includes both branches");
            assert(await evaluate(`retainedGraphEntry === document.querySelector('.plugin[data-node="3"]')`), "Selecting within a chain preserves its list");
            await click('.graph-node[data-node="3"]');
            await click('#views'); await click('#views');
            assert.equal(await evaluate(`document.querySelector('.plugin[data-node="3"] .plugin-body').textContent`), 'Inputs: Source <&>');
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 0, "Mix inspection opens no plugin UI");
            await click('.graph-node[data-node="0"]');
            await wait(`${effectUI}?.querySelectorAll('.perone-controls label').length === 3`);
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
            await click("#score-tab");
            await click('.track-select[data-track="0"]');
            await wait(`document.querySelector('.plugin[data-node="1"] .plugin-body > div')?.shadowRoot?.querySelectorAll('.perone-controls label').length === 3`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin").length'), 1, "Track selection restores its plugin chain");
            await click("#graph-tab");
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
            const masterSource = source.replace('(daw/master (daw/mix [dry wet dry] {:id :output}))',
                `(daw/master (daw/mix [dry wet dry] {:id :output}) {:effects
                  [(daw/plugin :master1 "build/test/effect.perone")
                   (daw/plugin :master2 "build/test/effect.perone")]})`);
            await set("#code", masterSource);
            await click("#play");
            await wait('document.querySelectorAll(".graph-node").length === 6');
            await click("#graph-fit");
            await click('.graph-node[data-node="0"]');
            await wait(`${effectUI}?.querySelectorAll('.perone-controls label').length === 3`);
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin"), p => Number(p.dataset.node))'), [1, 0, 5, 6, 7],
                "The sidebar includes both downstream master effects");
            assert.deepEqual(await openNodes(), [0]);
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".graph-node:not(.dim)"), p => Number(p.dataset.node)).sort((a,b) => a-b)'), [0, 1, 5, 6, 7],
                "Graph highlighting and the sidebar describe the same path");
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
