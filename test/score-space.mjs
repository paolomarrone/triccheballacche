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
            await wait('document.querySelector("#play")?.disabled === false');
            await wait('document.querySelector("#score-canvas").dataset.projection === "2d"');
            assert.equal(await evaluate('document.querySelectorAll("#timeline canvas").length'), 1);
            await click("#score-canvas");
            assert.deepEqual(diagnostics, [], "An empty 2D score remains interactive before playback");
            await click("#score-3d");
            await evaluate(`(() => {
                const split = document.querySelector('#split');
                for (let i = 0; i < 16; ++i) split.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowUp'}));
            })()`);
            assert(await evaluate('document.querySelector("#timeline").clientHeight > 500'), "The divider expands the score panel");
            await click("#play");
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            await wait('Number(document.querySelector("#score-canvas").dataset.automation) === 10');
            await wait('Number(document.querySelector("#score-canvas").dataset.active) > 0');
            await click("#stop");
            const projection = await evaluate('({...document.querySelector("#score-canvas").dataset})');
            assert(Number(projection.notes) > 10, "Include all source tracks, beyond a single eight-lane response");
            assert.equal(await evaluate('document.querySelector("#score-message").hidden'), true);
            await click("#score-3d");
            assert.equal(await evaluate('document.querySelector("#score-3d").checked'), false);
            const painted = () => evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
            const hoverNote = async (track, late = false) => {
                await painted();
                const hit = await evaluate(`(() => {
                    const c = document.querySelector('#score-canvas'), r = c.getBoundingClientRect(), d = c.dataset;
                    const lane = document.querySelectorAll('#track-list .track')[${track}].getBoundingClientRect();
                    const height = Math.max(2, Math.min(9, (lane.height - 16) / 17));
                    const x = r.left + (${late ? 7.25 : 2} - Number(d.from)) / (Number(d.to) - Number(d.from)) * r.width;
                    const y = lane.top + lane.height - 8 - ${late ? 14 : 2} * (lane.height - 16) / 17 - height / 2;
                    c.dispatchEvent(new PointerEvent('pointermove', {clientX: x, clientY: y}));
                    return {x, y, title: c.title};
                })()`);
                assert.match(hit.title, new RegExp(`Track ${track + 1} · .*MIDI ${60 + track + (late ? 12 : 0)}`),
                    "2D note coordinates must align exactly with the DOM track rows");
                return {x: hit.x, y: hit.y};
            };
            const wheel = (deltaY, modifiers = {}, target = "#score-canvas") => evaluate(`(() => {
                const c = document.querySelector(${JSON.stringify(target)}), r = c.getBoundingClientRect();
                c.dispatchEvent(new WheelEvent('wheel', {bubbles: true, cancelable: true, clientX: r.left + 100,
                    clientY: r.top + 24, deltaY: ${deltaY}, ...${JSON.stringify(modifiers)}}));
            })()`);
            const flatView = () => evaluate(`(() => {
                const d = document.querySelector('#score-canvas').dataset;
                return {span: Number(d.to) - Number(d.from), from: Number(d.from), scroll: document.querySelector('#roll').scrollTop,
                    row: document.querySelector('#track-list .track').getBoundingClientRect().height};
            })()`);
            const early = await hoverNote(0), late = await hoverNote(0, true);
            assert(early.x < late.x, "2D time runs from left to right");
            await hoverNote(2);
            await hoverNote(5);
            const initial = await flatView();
            await wheel(-100);
            await painted();
            const zoomed = await flatView();
            assert(zoomed.span < initial.span && zoomed.row === initial.row, "Wheel zooms time without resizing tracks");
            await wheel(-100, {ctrlKey: true});
            await painted();
            const taller = await flatView();
            assert(taller.row > initial.row && Math.abs(taller.span - zoomed.span) < 1e-6, "Ctrl+wheel expands tracks without changing time");
            await hoverNote(2);
            await wheel(180, {}, "#track-headers");
            await painted();
            assert((await flatView()).scroll > 0, "The track names scroll both labels and the 2D score");
            await hoverNote(3);
            await wheel(100, {metaKey: true});
            await painted();
            assert(Math.abs((await flatView()).row - initial.row) < 1, "Command+wheel can shrink the tracks again");
            await hoverNote(3);
            const panStart = await hoverNote(3), beforePan = await flatView();
            await call("Input.dispatchMouseEvent", {type: "mousePressed", ...panStart, button: "left", clickCount: 1});
            await call("Input.dispatchMouseEvent", {type: "mouseMoved", x: panStart.x - 40, y: panStart.y - 40, buttons: 1});
            await call("Input.dispatchMouseEvent", {type: "mouseReleased", x: panStart.x - 40, y: panStart.y - 40, button: "left", clickCount: 1});
            await painted();
            const panned = await flatView();
            assert(panned.from > beforePan.from && panned.scroll > beforePan.scroll, "Dragging pans both axes in 2D");
            await wheel(100);
            await evaluate('document.querySelector("#roll").scrollTop = 0');
            await wheel(1000, {ctrlKey: true});
            await painted();
            assert.equal((await flatView()).row, 44, "Compact tracks keep their controls usable");
            await wait('document.querySelector("#score-canvas").dataset.automation === "10"');
            await hoverNote(9);
            const selectedNote = await hoverNote(0);
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, ...selectedNote, button: "left", clickCount: 1});
            await wait('document.querySelector("#note-info").title.length > 0');
            assert.match(await evaluate('document.querySelector("#code").value.slice(document.querySelector("#code").selectionStart, document.querySelector("#code").selectionEnd)'), /\[:note/);
            const curve = await evaluate(`(() => {
                const c = document.querySelector('#score-canvas'), r = c.getBoundingClientRect(), d = c.dataset;
                const lane = document.querySelector('#track-list .track').getBoundingClientRect();
                return {x: r.left + (2 - Number(d.from)) / (Number(d.to) - Number(d.from)) * r.width,
                    y: lane.bottom - 8 - .002 * (lane.height - 16)};
            })()`);
            await call("Input.dispatchMouseEvent", {type: "mouseMoved", ...curve});
            assert.match(await evaluate('document.querySelector("#score-canvas").title'), /Intensity .*0[.,]002/);
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, ...curve, button: "left", clickCount: 1});
            await wait('document.querySelector("#code").value.slice(document.querySelector("#code").selectionStart, document.querySelector("#code").selectionEnd).includes("[:param")');
            await writeFile(`build/test/score-flat-${mode}.png`, Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            const rulerPoint = {...curve, y: await evaluate('document.querySelector("#score-canvas").getBoundingClientRect().top + 10')};
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, ...rulerPoint, button: "left", clickCount: 1});
            await wait('Math.abs(Number(document.querySelector("#score-canvas").dataset.time) - 2) < .05');
            await click("#follow");
            await click("#score-3d");
            await click("#score-reset");
            await painted();
            await wait('document.querySelector("#score-canvas").dataset.automation === "10"');
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
            await wait('document.querySelector("#code").value.slice(document.querySelector("#code").selectionStart, document.querySelector("#code").selectionEnd).includes("[:note")');
            await click('#track-list [data-listen="1"]');
            await wait('document.querySelector("#track-list .track-listen button").getAttribute("aria-pressed") === "true"');
            await click("#show-automation");
            await wait('document.querySelector("#score-canvas").dataset.automation === "0"');
            await click("#score-3d");
            await wait('document.querySelector("#score-canvas").dataset.projection === "2d" && document.querySelector("#score-canvas").dataset.automation === "0"');
            assert.equal(await evaluate('document.querySelector("#track-list .track-listen button").getAttribute("aria-pressed")'), "true");
            assert.equal(await evaluate('document.querySelector("#timeline").dataset.revision'), projection.revision);
            await click("#show-automation");
            await wait('document.querySelector("#score-canvas").dataset.automation === "10"');
            const seek = async seconds => {
                await wait('!document.querySelector("#time").disabled');
                await evaluate(`(() => { const t = document.querySelector('#time'); t.focus(); t.value = ${seconds}; })()`);
                await key("Enter");
                await wait(`!document.querySelector('#time').disabled && Number(document.querySelector('#score-canvas').dataset.time) === ${seconds}`);
            };
            await seek(1000000);
            await wait('Number(document.querySelector("#score-canvas").dataset.from) > 999900 && Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            assert(await evaluate('Number(document.querySelector("#score-canvas").dataset.notes) <= 512 * 11'), "Unbounded scores keep a bounded projection");
            await seek(0);
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            // Follow scrolls continuously across buffer refills, including a slow native bridge.
            for (let i = 0; i < 3; ++i)
                await evaluate('document.querySelector("#score-canvas").dispatchEvent(new KeyboardEvent("keydown", {key: "+"}))');
            await seek(50);
            await wait('Number(document.querySelector("#score-canvas").dataset.bufferFrom) > 40 && document.querySelector("#score-canvas").dataset.automation === "10"');
            await evaluate(`(() => {
                const upload = WebGL2RenderingContext.prototype.bufferData;
                window.uploads = 0;
                WebGL2RenderingContext.prototype.bufferData = function(...args) { ++window.uploads; return upload.apply(this, args); };
                if (!window.webui) return;
                const call = webui.call.bind(webui);
                window.delayRange = true;
                webui.call = async (...args) => {
                    const result = await call(...args);
                    if (args[1] === 'range' && window.delayRange) await new Promise(resolve => setTimeout(resolve, 100));
                    return result;
                };
            })()`);
            await click("#play");
            const frames = await evaluate(`new Promise(resolve => {
                const frames = [], start = performance.now(), c = document.querySelector('#score-canvas');
                function sample() {
                    const d = c.dataset;
                    frames.push([d.from, d.to, d.time, d.bufferFrom, d.bufferTo, uploads, d.notes, d.automation].map(Number));
                    if (performance.now() - start < 4600) requestAnimationFrame(sample);
                    else resolve(frames);
                }
                requestAnimationFrame(sample);
            })`);
            await click("#stop");
            await evaluate('window.delayRange = false');
            assert(frames.every(([from, to, time, a, b, , notes, curves]) =>
                a <= from && b >= to && notes > 0 && curves === 10 && Math.abs((time - from) / (to - from) - .15) < .001),
            "Prefetched notes and automation cover the viewport under a fixed playhead");
            const windows = new Set(frames.map(f => f[3]));
            assert(windows.size >= 2 && windows.size < 6, "Refill a bounded buffer ahead of playback");
            let moving = 0, reused = 0;
            for (let i = 1; i < frames.length; ++i) {
                const current = frames[i], previous = frames[i - 1], advance = current[0] - previous[0];
                assert(Math.abs(advance) < .4, "Follow must not jump between time pages");
                if (advance > 0) {
                    ++moving;
                    if (current[3] === previous[3] && current[5] === previous[5]) ++reused;
                }
            }
            assert(moving > 50 && reused > moving * .8, "Scrolling reuses GPU geometry between refills");
            await wait('document.querySelector("#stop").disabled && !document.querySelector("#play").disabled');
            await click("#follow");
            await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
            const parked = await evaluate('document.querySelector("#score-canvas").dataset.from');
            await click("#play");
            await wait(`Number(document.querySelector('#score-canvas').dataset.time) > ${frames.at(-1)[2] + .3}`);
            assert.equal(await evaluate('document.querySelector("#score-canvas").dataset.from'), parked, "Disabling Follow keeps the viewport stationary");
            await click("#stop");
            await click("#follow");
            for (let i = 0; i < 3; ++i)
                await evaluate('document.querySelector("#score-canvas").dispatchEvent(new KeyboardEvent("keydown", {key: "-"}))');
            await seek(0);
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0 && document.querySelector("#score-canvas").dataset.automation === "10"');
            await click("#play");
            await set("#code", source.replace("(+ 60 i)", "(+ 48 i)"));
            await click("#play");
            await wait(`Number(document.querySelector('#score-canvas').dataset.revision) > ${projection.revision}`);
            assert.equal(await evaluate('document.querySelector("#track-list .track-listen button").getAttribute("aria-pressed")'), "true", "Live revisions retain audition state");
            const panelHeight = await evaluate('document.querySelector("#timeline").clientHeight');
            await click("#score-3d");
            await wait('document.querySelector("#score-canvas").dataset.projection === "3d"');
            assert.equal(await evaluate('document.querySelector("#timeline").clientHeight'), panelHeight, "Changing projection preserves panel height");
            await click("#stop");
            const revision = await evaluate('document.querySelector("#score-canvas").dataset.revision');
            await set("#code", '(error "invalid spatial draft")');
            await click("#play");
            await wait('document.querySelector("#errors").textContent.includes("invalid spatial draft")');
            assert.equal(await evaluate('document.querySelector("#score-canvas").dataset.revision'), revision);
            assert(await evaluate('Number(document.querySelector("#score-canvas").dataset.notes) > 0'), "Failed evaluation retains the last projection");
            await call("Emulation.setEmulatedMedia", {features: [{name: "prefers-color-scheme", value: "dark"}]});
            await writeFile(`build/test/score-space-${mode}.png`, Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            await set("#code", source.replace("(+ 60 i)", "(+ 48 i)"));
            // Losing graphics must not interrupt playback or prevent context recovery.
            const extension = await evaluate(`(() => {
                const gl = document.querySelector('#score-canvas').getContext('webgl2');
                window.loseContext = gl.getExtension('WEBGL_lose_context');
                if (!loseContext) return false;
                loseContext.loseContext(); return true;
            })()`);
            assert(extension);
            await wait('document.querySelector("#score-message").textContent.includes("context lost") && !document.querySelector("#score-message").hidden');
            await click("#play");
            const lostTime = await evaluate('Number(document.querySelector("#time").value)');
            await wait(`Number(document.querySelector('#time').value) > ${lostTime + .2}`);
            await click("#stop");
            await evaluate('loseContext.restoreContext()');
            await wait('document.querySelector("#score-message").hidden');
            await click("#score-3d");
            await wait('document.querySelector("#score-canvas").dataset.projection === "2d"');
            await set("#code", `(def s (daw/plugin "build/test/fixture.perone" {:gain 0.001}))
(daw/output (daw/track s))
(for i 0 1300
  (daw/note s (* i 0.001) 0.0005 (+ 48 (% i 24)) 80)
  (daw/param s (* i 0.001) :gain (* 0.001 (% i 2))))
(daw/end 5)`);
            await click("#play");
            await wait('document.querySelector("#score-canvas").dataset.dense === "true" && document.querySelector("#score-canvas").dataset.automationDense === "true"');
            await click("#stop");
            assert.deepEqual(diagnostics, []);
            await evaluate("window.onbeforeunload = null");
            await call("Page.navigate", {url: "about:blank"});
            console.log(`OK: ${mode} 2D/3D score, aligned/scalable tracks, navigation, sources, automation, follow, live revisions and context restoration`);
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
        await wait('document.querySelector("#play")?.disabled === false');
        await wait('document.querySelector("#score-message").textContent.includes("unavailable")');
        await click("#play");
        await wait('Number(document.querySelector("#time").value) > .2');
        assert(await evaluate('document.querySelector("#score-3d").disabled'));
        assert(!await evaluate('document.querySelector("#score-message").hidden'));
        assert.equal(await evaluate('document.querySelector("#errors").hidden'), true);
        await wait('document.querySelectorAll(".routing-node").length === 11');
        assert.equal(await evaluate('document.querySelectorAll(".routing-cable").length'), 10);
        await click("#stop");
        assert.deepEqual(diagnostics, []);
        await call("Page.navigate", {url: "about:blank"});
        console.log("OK: editing, playback and routing remain usable without WebGL");
    });
} finally {
    if (app) await app.close();
    if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
    await rm(directory, {recursive: true, force: true});
}
