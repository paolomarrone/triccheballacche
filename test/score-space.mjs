import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

const directory = await mkdtemp("build/test/score-space-"), entry = `${directory}/score.janet`;
const source = `(import ../../../lib/pattern :as p)
(def tracks @[])
(def events @[])
(for i 0 10
  (def s (daw/plugin (keyword (string "s" i)) "build/test/fixture.perone" {:gain 0.001}))
  (array/push tracks (daw/track s {:name (string "Voice " (+ i 1))}))
  (array/push events [0 6 [:note s (+ 60 i) 80]] [7 7.5 [:note s (+ 72 i) 90]])
  (array/push events [1 1 [:param s :gain 0.002]]))
(daw/output (daw/master (daw/mix tracks {:id :bus})))
(daw/tempo 60)
(daw/score (p/loop (p/events 8 events)) {:quantum 0.25})
`;
let app, server;
try {
    await writeFile(entry, source);
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", entry, "build/test/fixture.perone"]);
    server = serve(0); await once(server, "listening");
    for (const mode of ["native", "web"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") { app = nativeEditor(entry); url = await app.url; }
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            await call("Emulation.setDeviceMetricsOverride", {width: 1300, height: 950, deviceScaleFactor: 1, mobile: false});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#run")?.disabled === false');
            assert(!await evaluate('performance.getEntriesByType("resource").some(r => r.name.includes("three.module"))'), "Tracks does not load Three.js");
            await click("#run");
            await wait('Number(document.querySelector("#notes").dataset.notes) > 0');
            await click("#tab-score");
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            await wait('Number(document.querySelector("#score-canvas").dataset.automation) === 10');
            await wait('Number(document.querySelector("#score-canvas").dataset.active) > 0');
            await click("#stop");
            const projection = await evaluate('({...document.querySelector("#score-canvas").dataset})');
            assert(Number(projection.notes) > 10, "Include all source tracks, beyond a single eight-lane response");
            assert.equal(await evaluate('document.querySelector("#score-message").hidden'), true);
            await click("#score-flat");
            assert.equal(await evaluate('document.querySelector("#score-flat").getAttribute("aria-pressed")'), "true");
            await click("#score-flat");
            await click("#score-reset");
            // Find a rendered note through real ray casting, then select its Janet origin.
            const point = await evaluate(`(() => {
                const c = document.querySelector('#score-canvas'), r = c.getBoundingClientRect();
                for (let y = r.top + 20; y < r.bottom - 20; y += 8) for (let x = r.left + 20; x < r.right - 20; x += 8) {
                    c.dispatchEvent(new PointerEvent('pointermove', {clientX: x, clientY: y}));
                    if (c.title.includes(' · MIDI ') && c.title.includes('–')) return {x, y};
                }
            })()`);
            assert(point, "Spatial notes must be pickable");
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, ...point, button: "left", clickCount: 1});
            await wait('document.querySelector("#note-info").title.length > 0');
            assert.match(await evaluate('document.querySelector("#code").value.slice(document.querySelector("#code").selectionStart, document.querySelector("#code").selectionEnd)'), /\[:note/);
            await click('#track-list [data-listen="1"]');
            await wait('document.querySelector("#track-list .track-listen button").getAttribute("aria-pressed") === "true"');
            await click("#show-automation");
            await wait('document.querySelector("#score-canvas").dataset.automation === "0"');
            await click("#tab-tracks");
            await wait('document.querySelector("#notes").dataset.automation === "0"');
            assert.equal(await evaluate('document.querySelector("#track-list .track-listen button").getAttribute("aria-pressed")'), "true");
            assert.equal(await evaluate('document.querySelector("#timeline").dataset.revision'), projection.revision);
            await click("#show-automation");
            await click("#tab-score");
            await wait('document.querySelector("#score-canvas").dataset.automation === "10"');
            const seek = async seconds => {
                await evaluate(`(() => { const t = document.querySelector('#time'); t.focus(); t.value = ${seconds}; })()`);
                await key("Enter");
                await wait(`!document.querySelector('#time').disabled && Number(document.querySelector('#score-canvas').dataset.time) === ${seconds}`);
            };
            await seek(1000000);
            await wait('Number(document.querySelector("#score-canvas").dataset.from) > 999900 && Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            assert(await evaluate('Number(document.querySelector("#score-canvas").dataset.notes) <= 512 * 11'), "Unbounded scores keep a bounded projection");
            await seek(0);
            await click("#play");
            await set("#code", source.replace("(+ 60 i)", "(+ 48 i)"));
            await click("#run");
            await wait(`Number(document.querySelector('#score-canvas').dataset.revision) > ${projection.revision}`);
            assert.equal(await evaluate('document.querySelector("#track-list .track-listen button").getAttribute("aria-pressed")'), "true", "Live revisions retain audition state");
            await click("#tab-tracks");
            const hiddenTime = await evaluate('document.querySelector("#score-canvas").dataset.time');
            await wait(`Number(document.querySelector('#time').value) > ${Number(hiddenTime) + .2}`);
            assert.equal(await evaluate('document.querySelector("#score-canvas").dataset.time'), hiddenTime, "Hidden score does not keep rendering");
            await click("#tab-score");
            await wait(`Number(document.querySelector('#score-canvas').dataset.time) > ${hiddenTime}`);
            await click("#stop");
            const revision = await evaluate('document.querySelector("#score-canvas").dataset.revision');
            await set("#code", '(error "invalid spatial draft")');
            await click("#run");
            await wait('document.querySelector("#errors").textContent.includes("invalid spatial draft")');
            assert.equal(await evaluate('document.querySelector("#score-canvas").dataset.revision'), revision);
            assert(await evaluate('Number(document.querySelector("#score-canvas").dataset.notes) > 0'), "Failed evaluation retains the last projection");
            await call("Emulation.setEmulatedMedia", {features: [{name: "prefers-color-scheme", value: "dark"}]});
            await writeFile(`build/test/score-space-${mode}.png`, Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            // An unavailable WebGL context must not take down the editor or Tracks.
            const extension = await evaluate(`(() => {
                const gl = document.querySelector('#score-canvas').getContext('webgl2');
                window.loseContext = gl.getExtension('WEBGL_lose_context');
                if (!loseContext) return false;
                loseContext.loseContext(); return true;
            })()`);
            assert(extension);
            await wait('document.querySelector("#score-message").textContent.includes("context lost") && !document.querySelector("#score-message").hidden');
            await click("#tab-tracks");
            await wait('Number(document.querySelector("#notes").dataset.notes) > 0');
            await evaluate('loseContext.restoreContext()');
            await click("#tab-score");
            await wait('document.querySelector("#score-message").hidden');
            await click("#tab-tracks");
            await key("ArrowRight");
            assert.equal(await evaluate('document.querySelector("#tab-score").getAttribute("aria-selected")'), "true");
            await set("#code", `(def s (daw/plugin "build/test/fixture.perone" {:gain 0.001}))
(daw/output (daw/track s))
(for i 0 1300
  (daw/note s (* i 0.001) 0.0005 (+ 48 (% i 24)) 80)
  (daw/param s (* i 0.001) :gain (* 0.001 (% i 2))))
(daw/end 5)`);
            await click("#run");
            await wait('document.querySelector("#score-canvas").dataset.dense === "true" && document.querySelector("#score-canvas").dataset.automationDense === "true"');
            await click("#stop");
            assert.deepEqual(diagnostics, []);
            await evaluate("window.onbeforeunload = null");
            await call("Page.navigate", {url: "about:blank"});
            console.log(`OK: ${mode} spatial score, source picking, shared automation/mute, seek, live revisions, recovery and context restoration`);
        }, {graphics: true});
        if (app) { await app.close(); app = undefined; }
    }
    await withBrowser(async ({call, wait, click, evaluate, diagnostics}) => {
        await call("Page.addScriptToEvaluateOnNewDocument", {source: `
            const context = HTMLCanvasElement.prototype.getContext;
            HTMLCanvasElement.prototype.getContext = function(type, ...args) {
                return type.startsWith('webgl') ? null : context.call(this, type, ...args);
            };`});
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${directory}/project.json`});
        await wait('document.querySelector("#run")?.disabled === false');
        await click("#tab-score");
        await wait('document.querySelector("#score-message").textContent.includes("unavailable")');
        await click("#tab-tracks");
        await click("#run");
        await wait('Number(document.querySelector("#notes").dataset.notes) > 0');
        assert.equal(await evaluate('document.querySelector("#errors").hidden'), true);
        await click("#stop");
        assert.deepEqual(diagnostics, []);
        await call("Page.navigate", {url: "about:blank"});
        console.log("OK: editor remains usable without WebGL");
    });
} finally {
    if (app) await app.close();
    if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
    await rm(directory, {recursive: true, force: true});
}
