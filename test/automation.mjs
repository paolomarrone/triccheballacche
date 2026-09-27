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
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            await call("Page.navigate", {url});
            await call("Emulation.setDeviceMetricsOverride", {width: 1100, height: 760, deviceScaleFactor: 1, mobile: false});
            await wait('document.querySelector("#run")?.disabled === false');
            await click("#run");
            await wait('document.querySelector("#notes").dataset.automation === "2"');
            await click("#stop");
            const control = ".track-automation";
            const open = async () => {
                await click(control);
                await wait('document.querySelector("#automation-parameters").matches(":popover-open")');
                await evaluate('new Promise(requestAnimationFrame)'); // Wait for the anchored popup's layout.
            };
            const selected = () => evaluate('Array.from(document.querySelectorAll("#automation-parameters input:checked:not([value=all])"), c => c.value)');
            const choose = value => click(`#automation-parameters input[value="${value}"]`);
            await open();
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll("#automation-parameters input"), c => c.value)'),
                ["all", "0:1", "1:1", "2:1"], "Offer automated parameters across this track's chain");
            assert.deepEqual(await selected(), ["1:1"],
                "Start with the most frequently automated parameter");
            assert(await evaluate('document.querySelector("#automation-parameters input[value=all]").indeterminate'));
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".track-automation"), s => s.hidden)'),
                [false, true, false], "Do not borrow parameters from upstream tracks");
            await key("Escape");
            await wait('!document.querySelector("#automation-parameters").matches(":popover-open")');
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
            await evaluate(`(() => {
                const c = document.querySelector('#notes'), r = c.getBoundingClientRect();
                c.dispatchEvent(new WheelEvent('wheel', {bubbles: true, cancelable: true, ctrlKey: true,
                    clientX: r.left + 300, clientY: r.top + 24, deltaY: 1000}));
            })()`);
            assert(await evaluate(`Array.from(document.querySelectorAll('.track-automation:not([hidden])')).every(s => {
                const r = s.getBoundingClientRect(), lane = s.closest('.track').getBoundingClientRect();
                return r.top >= lane.top && r.bottom <= lane.bottom;
            })`), "Selectors fit even at the smallest track height");
            await open();
            await choose("2:1");
            await wait('document.querySelector("#notes").dataset.automation === "3"');
            assert.deepEqual(await selected(), ["1:1", "2:1"]);
            await choose("all");
            await wait('document.querySelector("#notes").dataset.automation === "4"');
            assert.deepEqual(await selected(), ["0:1", "1:1", "2:1"]);
            assert(!await evaluate('document.querySelector("#automation-parameters input[value=all]").indeterminate'));
            assert.equal(await evaluate('new Set(Array.from(document.querySelectorAll("#automation-parameters i"), i => i.style.getPropertyValue("--curve-hue"))).size'), 3);
            const screenshot = await call("Page.captureScreenshot", {format: "png"});
            await writeFile(`build/test/automation-${mode}.png`, Buffer.from(screenshot.data, "base64"));
            await choose("0:1");
            await wait('document.querySelector("#notes").dataset.automation === "3"');
            await key("Escape");
            await click("#show-automation");
            await wait('document.querySelector("#notes").dataset.automation === "0"');
            assert.equal(await evaluate('document.querySelector("#show-automation").getAttribute("aria-pressed")'), "false");
            await open();
            assert.deepEqual(await selected(), ["1:1", "2:1"], "Global visibility retains every selected parameter");
            await click(control);
            await wait('!document.querySelector("#automation-parameters").matches(":popover-open")');
            await click("#show-automation");
            await wait('document.querySelector("#notes").dataset.automation === "3"');
            await click("#play");
            const revision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
            await set("#code", source.replace(":gain 0.03", ":gain 0.02"));
            await click("#run");
            await wait(`Number(document.querySelector('#timeline').dataset.revision) > ${revision}`);
            await open();
            assert.deepEqual(await selected(), ["1:1", "2:1"], "Multiple selections survive live revisions");
            await key("Escape");
            await wait('!document.querySelector("#automation-parameters").matches(":popover-open")');
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false, "Escape closes the checklist without stopping audio");
            await click("#stop");
            const pan = async from => evaluate(`(() => {
                const c = document.querySelector('#notes'), r = c.getBoundingClientRect();
                c.dispatchEvent(new WheelEvent('wheel', {bubbles: true, cancelable: true, shiftKey: true,
                    clientX: r.left + 300, deltaY: (${from} - Number(c.dataset.from)) / Number(c.dataset.scale)}));
            })()`);
            if (mode === "native") {
                await evaluate(`(() => {
                    const original = webui.call.bind(webui);
                    window.automationCalls = window.rangeCalls = 0;
                    webui.call = async (...args) => {
                        const result = await original(...args);
                        if (args[1] === 'range') ++window.rangeCalls;
                        if (args[1] === 'automation' && ++window.automationCalls === 1)
                            await new Promise(resolve => window.releaseAutomation = resolve);
                        return result;
                    };
                })()`);
                await pan(2);
                await wait('typeof window.releaseAutomation === "function"');
                await click("#show-automation");
                await wait('document.querySelector("#notes").dataset.automation === "0"');
                const ranges = await evaluate('window.rangeCalls');
                await evaluate('window.releaseAutomation()');
                await wait(`window.rangeCalls > ${ranges}`);
                assert.equal(await evaluate('window.automationCalls'), 1, "Hiding automation cancels the remaining curve requests");
                assert.equal(await evaluate('document.querySelector("#notes").dataset.automation'), "0", "A late reply cannot restore hidden curves");
                await click("#show-automation");
                await wait('document.querySelector("#notes").dataset.automation === "3"');
            }
            await pan(1e9);
            await wait('Number(document.querySelector("#notes").dataset.from) === 1e9 && document.querySelector("#notes").dataset.automation === "3"');
            await pan(0);
            // Wide periodic views stay bounded and switch from steps to a peak-preserving overview.
            for (let i = 0; i < 9; ++i)
                await evaluate('document.querySelector("#notes").dispatchEvent(new KeyboardEvent("keydown", {key: "-"}))');
            await wait('document.querySelector("#notes").dataset.automationDense === "true"');
            assert(await evaluate('document.querySelector("#notes").width <= 1100'));
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            assert.deepEqual(diagnostics, []);
            console.log(`OK: ${mode} automation checklists, global visibility, log scale, origins, live revisions and distant/dense loops`);
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
