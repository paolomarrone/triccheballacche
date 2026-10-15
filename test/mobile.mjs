import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

const directory = await mkdtemp("build/test/mobile-"), entry = `${directory}/score.janet`;
const source = `# Source
(import ../../../lib/pattern :as p)
(def tracks @[])
(def events @[])
(for i 0 10
  (def s (daw/plugin (keyword (string "s" i)) "build/test/fixture.perone" {:gain 0.001}))
  (array/push tracks (daw/track s {:name (string "Voice " (+ i 1))}))
  (array/push events [0 6 [:note s (+ 60 i) 80]]))
(daw/output (daw/master (daw/mix tracks {:id :bus})))
(daw/tempo 60)
(daw/score (p/loop (p/events 8 events)) {:quantum 0.25})
` + "# Scrollable source\n".repeat(100);
let app, server;
try {
    await writeFile(entry, source);
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", entry, "build/test/fixture.perone"]);
    server = serve(0); await once(server, "listening");
    for (const mode of ["native", "web"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") { app = nativeEditor(entry); url = await app.url; }
        await withBrowser(async ({call, evaluate, wait, selectText, diagnostics}) => {
            const size = async (width, height) => {
                await call("Emulation.setDeviceMetricsOverride", {width, height, deviceScaleFactor: 1, mobile: true});
                await wait(`!document.querySelector('#transport') || Math.abs(document.body.getBoundingClientRect().height - ${height}) < 1`);
            };
            const touch = (type, points = []) => call("Input.dispatchTouchEvent", {type,
                touchPoints: points.map(([x, y], id) => ({x, y, id}))});
            const rect = selector => evaluate(`document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect().toJSON()`);
            const tap = async selector => {
                const r = await rect(selector);
                assert(r.width > 0 && r.height > 0, `${selector} is visible`);
                await touch("touchStart", [[r.x + r.width / 2, r.y + r.height / 2]]);
                await touch("touchEnd");
            };
            const painted = () => evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
            const view = () => evaluate(`(() => {
                const d = document.querySelector('#score-canvas').dataset;
                return {from: Number(d.from), span: Number(d.to) - Number(d.from), scroll: document.querySelector('#roll').scrollTop};
            })()`);
            const screenshot = async name => {
                if (process.env.SCREENSHOTS) await writeFile(`build/test/mobile-${mode}-${name}.png`,
                    Buffer.from((await call("Page.captureScreenshot")).data, "base64"));
            };
            await size(390, 844);
            await call("Emulation.setTouchEmulationEnabled", {enabled: true, maxTouchPoints: 5});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#play")?.disabled === false');
            assert.equal((await rect("#timeline")).height, 0);
            assert((await rect("#sheet")).height > 600);
            await selectText("#code", 2, 8);
            await call("Input.insertText", {text: "Mobile"});
            await wait('document.querySelector("#modified").textContent === "●"');
            const text = await evaluate('document.querySelector("#code").textContent');
            assert.equal(text, source.replace("Source", "Mobile"));
            await selectText("#code", 2, 8);
            await evaluate('document.querySelector("#code").scrollTop = 600');
            await tap("#view-score");
            await painted();
            assert.equal((await rect("#sheet")).height, 0);
            await tap("#view-code");
            assert.equal(await evaluate('document.querySelector("#code").textContent'), text);
            assert.equal(await evaluate('getSelection().toString()'), "Mobile");
            assert.equal(await evaluate('document.querySelector("#code").scrollTop'), 600);
            await screenshot("code");

            await tap("#play");
            await wait('Number(document.querySelector("#time").value) > .2');
            await tap("#view-score");
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            await tap("#stop");
            const revision = await evaluate('document.querySelector("#timeline").dataset.revision');
            await tap("#show-routing");
            await painted();
            assert.equal((await rect("#track-headers")).width, 0);
            const r = await rect("#score-canvas"), x = r.x + r.width / 2, y = r.y + 200;
            const beforePan = await view();
            await touch("touchStart", [[x, y]]);
            await touch("touchMove", [[x - 50, y - 100]]);
            await touch("touchEnd");
            await painted();
            const panned = await view();
            assert(panned.from > beforePan.from && panned.scroll >= 99, "Dragging pans time and tracks");
            const anchor = panned.from + panned.span / 2;
            await touch("touchStart", [[x - 40, y], [x + 40, y]]);
            await touch("touchMove", [[x - 80, y], [x + 80, y]]);
            await touch("touchEnd");
            await painted();
            const zoomed = await view();
            assert(Math.abs(zoomed.span - panned.span / 2) < .01, "Pinching zooms time");
            assert(Math.abs(zoomed.from + zoomed.span / 2 - anchor) < .01, "Pinch preserves the time under its midpoint");
            assert.equal(await evaluate('document.body.dataset.view'), "score", "A pinch does not pick a note");
            assert.equal(await evaluate('document.querySelector("#views").checked'), false);
            await touch("touchStart", [[x, y]]);
            await touch("touchMove", [[x - 20, y]]);
            await touch("touchCancel");
            const canceled = await view();
            await touch("touchStart", [[x, r.y + 10]]);
            await touch("touchEnd");
            await wait(`Math.abs(Number(document.querySelector('#time').value) - ${canceled.from + canceled.span / 2}) < .02`);
            assert.equal(await evaluate('document.querySelector("#errors").hidden'), true);
            await tap("#view-code");
            await tap("#view-score");
            await painted();
            assert.deepEqual(await view(), canceled, "Switching views preserves the score viewport");
            assert.equal(await evaluate('document.querySelector("#timeline").dataset.revision'), revision);
            await screenshot("score");

            await tap("#show-routing");
            await painted();
            await screenshot("routing");
            await tap('.track-select[data-track="2"]');
            await wait('document.querySelector("#plugins").matches(":modal")');
            await wait('document.querySelector(".plugin-body > div")?.shadowRoot.querySelector("input")');
            await evaluate('globalThis.pluginView = document.querySelector(".plugin-body > div")');
            await screenshot("plugins");
            await size(1100, 800);
            await wait('!document.querySelector("#plugins").matches(":modal")');
            assert((await rect("#sheet")).height > 0 && (await rect("#timeline")).height > 0);
            assert.equal((await rect("#plugins")).width, 360);
            assert(await evaluate('globalThis.pluginView === document.querySelector(".plugin-body > div")'), "Resizing retains the mounted plugin UI");
            await size(320, 650);
            await wait('document.querySelector("#plugins").matches(":modal")');
            await tap("#plugins-close");
            assert.equal(await evaluate('document.querySelector("#views").checked'), false);
            assert(await evaluate('document.querySelector("#plugins").hidden'));
            assert(await evaluate('document.documentElement.scrollWidth <= innerWidth'), "The phone layout fits without horizontal scrolling");
            await tap("#settings");
            assert(await evaluate('document.querySelector("#settings-dialog").matches(":modal")'));
            const settings = await rect("#settings-dialog");
            assert(settings.left >= 0 && settings.right <= 320);
            await tap("#settings-dialog [data-close]");
            // A short viewport approximates the space remaining above a phone keyboard.
            await size(390, 360);
            await tap("#view-code");
            await painted();
            await screenshot("short");
            await wait('document.querySelector("#transport").getBoundingClientRect().bottom <= 360 && document.querySelector("#sheet").clientHeight > 100');
            await tap("#play");
            await wait('document.querySelector("#state").textContent.startsWith("Playing")');
            assert.equal(await evaluate('document.querySelector("#timeline").dataset.revision'), revision, "View changes do not re-evaluate the score");
            await tap("#stop");
            await size(844, 390);
            assert.equal((await rect("#timeline")).height, 0, "A phone in landscape still shows one view at a time");
            await tap("#view-score");
            await tap("#score-3d");
            await wait('document.querySelector("#score-canvas").dataset.projection === "3d"');
            const landscape = await rect("#score-canvas"), a = landscape.x + 70, b = landscape.y + 70;
            await touch("touchStart", [[a, b], [a + 60, b]]);
            await touch("touchMove", [[a - 20, b + 10], [a + 80, b + 10]]);
            await touch("touchEnd");
            assert.equal(await evaluate('document.body.dataset.view'), "score", "3D gestures do not pick notes");
            assert.equal(await evaluate('document.querySelector("#views").checked'), false);
            assert.deepEqual(diagnostics, []);
            await evaluate('window.onbeforeunload = null');
            await call("Page.navigate", {url: "about:blank"});
            console.log(`OK: ${mode} mobile layout, editing, playback, pan/pinch, seek and plugin panel`);
        }, {graphics: true});
        if (app) { await app.close(); app = undefined; }
    }
} finally {
    if (app) await app.close();
    if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
    await rm(directory, {recursive: true, force: true});
}
