import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {cp, mkdtemp, readFile, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

const directory = await mkdtemp("build/test/automation-"), entry = `${directory}/score.janet`;
const source = `(import ../../../lib/pattern :as p)
(def s (daw/plugin :s "build/test/fixture.perone" {:gain 0.01}))
(def f (daw/plugin :f "${directory}/effect.perone" {:gain 0.01}))
(def a (daw/track s {:effects [f] :name "Lead"}))
(def b (daw/track (daw/plugin :b "build/test/fixture.perone" {:gain 0}) {:name "Silent"}))
(def master (daw/master (daw/mix [a b] {:id :bus})))
(daw/output master)
(daw/tempo 60)
(def events @[
  [0 1 [:note s 60 80]]
  [0.5 0.5 [:param s :gain 0.03]]
  [0 0 [:param f :gain 0.01]]
  [0 0 [:param a :pan -0.5]]
  [1 1 [:param a :pan 0.5]]
  [0 0 [:param master :gain 1]]])
(array/push events [0.5 0.5 [:param f :gain 0.1]])
(daw/score (p/loop (p/events 2 events)) {:quantum 0.25})
`;
let app, server;
try {
    const bundle = `${directory}/effect.perone`;
    await cp("build/test/effect.perone", bundle, {recursive: true});
    const metadata = JSON.parse(await readFile(`${bundle}/product.json`, "utf8"));
    Object.assign(metadata.product.parameters[1], {minimum: .001, map: "logarithmic"});
    await writeFile(`${bundle}/product.json`, JSON.stringify(metadata));
    await writeFile(entry, source);
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", entry, bundle, "build/test/fixture.perone"]);
    server = serve(0);
    await once(server, "listening");
    for (const mode of ["native", "web"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") { app = nativeEditor(entry); url = await app.url; }
        await withBrowser(async ({call, evaluate, wait, click, set, diagnostics}) => {
            await call("Page.navigate", {url});
            await call("Emulation.setDeviceMetricsOverride", {width: 1100, height: 760, deviceScaleFactor: 1, mobile: false});
            await wait('document.querySelector("#run")?.disabled === false');
            await click("#run");
            await wait('document.querySelector("#notes").dataset.automation === "2"');
            await click("#stop");
            const control = ".track-automation";
            assert.deepEqual(await evaluate('Array.from(document.querySelector(".track-automation").options, o => o.value)'),
                ["", "0:1", "1:1", "2:1"], "Offer automated parameters across this track's chain");
            assert.equal(await evaluate('document.querySelector(".track-automation").value'), "1:1",
                "Start with the most frequently automated parameter");
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".track-automation"), s => s.hidden)'),
                [false, true, false], "Do not borrow parameters from upstream tracks");
            await set(control, "1:1");
            await wait('document.querySelector("#notes").dataset.automation === "2"');
            const point = await evaluate(`(() => {
                const c = document.querySelector('#notes'), r = c.getBoundingClientRect();
                const row = document.querySelector('.track').getBoundingClientRect().height;
                return {x: r.x + Math.min(230, Math.round(c.clientWidth * .3)) + .75 / Number(c.dataset.scale),
                    y: r.y + 24 + row - 8 - (2 / 3) * (row - 16)};
            })()`);
            await call("Input.dispatchMouseEvent", {type: "mouseMoved", ...point});
            assert.match(await evaluate('document.querySelector("#notes").title'), /0[.,]1 .*0.750 s/,
                "Use metadata's logarithmic scale and show the actual value");
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, ...point, button: "left", clickCount: 1});
            await wait('document.querySelector("#note-info").title.length > 0');
            assert.match(await evaluate(`(() => { const c = document.querySelector('#code'); return c.value.slice(c.selectionStart, c.selectionEnd); })()`),
                /param f :gain 0.1/, "A curve selects the code that generated its change");
            const screenshot = await call("Page.captureScreenshot", {format: "png"});
            await writeFile(`build/test/automation-${mode}.png`, Buffer.from(screenshot.data, "base64"));
            await evaluate(`(() => {
                const c = document.querySelector('#notes'), r = c.getBoundingClientRect();
                c.dispatchEvent(new WheelEvent('wheel', {bubbles: true, cancelable: true, ctrlKey: true,
                    clientX: r.left + 300, clientY: r.top + 24, deltaY: 1000}));
            })()`);
            assert(await evaluate(`Array.from(document.querySelectorAll('.track-automation:not([hidden])')).every(s => {
                const r = s.getBoundingClientRect(), lane = s.closest('.track').getBoundingClientRect();
                return r.top >= lane.top && r.bottom <= lane.bottom;
            })`), "Selectors fit even at the smallest track height");
            await set(control, "");
            await wait('document.querySelector("#notes").dataset.automation === "1"');
            await set(control, "2:1");
            await wait('document.querySelector("#notes").dataset.automation === "2"');
            await click("#play");
            const revision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
            await set("#code", source.replace(":gain 0.03", ":gain 0.02"));
            await click("#run");
            await wait(`Number(document.querySelector('#timeline').dataset.revision) > ${revision}`);
            assert.equal(await evaluate('document.querySelector(".track-automation").value'), "2:1");
            await click("#stop");
            const pan = async from => evaluate(`(() => {
                const c = document.querySelector('#notes'), r = c.getBoundingClientRect();
                c.dispatchEvent(new WheelEvent('wheel', {bubbles: true, cancelable: true, shiftKey: true,
                    clientX: r.left + 300, deltaY: (${from} - Number(c.dataset.from)) / Number(c.dataset.scale)}));
            })()`);
            await pan(1e9);
            await wait('Number(document.querySelector("#notes").dataset.from) === 1e9 && document.querySelector("#notes").dataset.automation === "2"');
            await pan(0);
            // Wide periodic views stay bounded and switch from steps to a peak-preserving overview.
            for (let i = 0; i < 9; ++i)
                await evaluate('document.querySelector("#notes").dispatchEvent(new KeyboardEvent("keydown", {key: "-"}))');
            await wait('document.querySelector("#notes").dataset.automationDense === "true"');
            assert(await evaluate('document.querySelector("#notes").width <= 1100'));
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            assert.deepEqual(diagnostics, []);
            console.log(`OK: ${mode} automation selection, log scale, origins, Stop/Play, live revisions and distant/dense loops`);
        });
        await app?.close();
        app = undefined;
    }
} finally {
    try { await app?.close(); }
    finally {
        if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
        await rm(directory, {recursive: true, force: true});
    }
}
