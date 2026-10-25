import {presets} from "./preset-ui.mjs";
import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {cp, mkdtemp, readFile, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

const directory = await mkdtemp("build/test/editor-ui-"), entry = `${directory}/score.janet`;
let server, app;
try {
    for (const [from, to] of [["fixture", "synth"], ["effect", "effect"]]) {
        const bundle = `${directory}/${to}.perone`;
        await cp(`build/test/${from}.perone`, bundle, {recursive: true});
        const metadata = JSON.parse(await readFile(`${bundle}/product.json`, "utf8"));
        if (to === "effect") {
            metadata.product.ui = {web: "ui/index.js"};
            metadata.product.messaging = {uiToDspSize: 16, dspToUiSize: 16};
            await cp("test/perone/ui", `${bundle}/ui`, {recursive: true});
            // Imported helper function: this UI Wasm must never enter the standalone DSP loader.
            await writeFile(`${bundle}/wasm32/fixture-ui.wasm`, new Uint8Array([
                0,97,115,109,1,0,0,0,1,5,1,96,0,1,127,
                2,16,1,6,104,101,108,112,101,114,5,118,97,108,117,101,0,0,
                7,9,1,5,118,97,108,117,101,0,0
            ]));
        }
        await writeFile(`${bundle}/product.json`, JSON.stringify(metadata));
    }
    await writeFile(entry, `(def s (daw/plugin "${directory}/synth.perone" {:gain 0.01}))
(def f (daw/plugin "${directory}/effect.perone" {:gain 0.4}))
(def a (daw/track s {:effects [f]}))
(def b (daw/track (daw/plugin "${directory}/synth.perone" {:gain 0.01})))
(daw/output (daw/master (daw/mix [a b])))
(daw/param f 1.5 :gain 0.2)
(daw/end 40)
`);
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", directory]);
    server = serve(0);
    await once(server, "listening");
    const wasm = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
    for (const mode of ["web", "native"]) {
        let url = wasm;
        if (mode === "native") {
            app = nativeEditor(entry);
            url = await app.url;
        }
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            const openEffect = async () => evaluate(`document.querySelector('.plugin[data-node="1"]').open = true`);
            const toggleParameters = async () => evaluate(`document.querySelector('.plugin[data-node="1"] button[aria-label="Toggle parameter controls"]').click()`);
            const root = `document.querySelector('.plugin[data-node="1"] .plugin-body > div')?.shadowRoot`;
            const synthRoot = `document.querySelector('.plugin[data-node="0"] .plugin-body > div')?.shadowRoot`;
            const fixture = `${root}?.querySelector(".fixture-ui")`;
            await call("Page.navigate", {url});
            await call("Emulation.setDeviceMetricsOverride", {width: 1100, height: 760, deviceScaleFactor: 1, mobile: false});
            await wait('document.querySelector("#play")?.disabled === false');
            await click("#views");
            await click("#play");
            await wait(`${synthRoot}?.querySelectorAll(".parameter-controls label").length === 3`);
            await presets({evaluate, wait, click}, mode === "native", directory);
            await click("#play");
            const toggle = async (track, bit, pressed) => {
                const button = `document.querySelector('#track-list .track:nth-child(${track + 1}) [data-listen="${bit}"]')`;
                await wait(`${button} && !${button}.disabled`);
                await evaluate(`${button}.click()`);
                await wait(`${button}.getAttribute('aria-pressed') === '${pressed}'`);
            };
            const inaudible = () => evaluate(`Array.from(document.querySelectorAll('#track-list .track'), t => t.classList.contains('inaudible'))`);
            assert.equal(await evaluate('document.querySelectorAll("#track-list [data-listen]").length'), 4, "The master has no track audition buttons");
            await toggle(0, 1, true);
            assert.deepEqual(await inaudible(), [true, false, false]);
            await toggle(1, 2, true);
            await toggle(0, 2, true);
            assert.deepEqual(await inaudible(), [true, false, false], "Mute takes precedence over solo");
            await toggle(0, 1, false);
            assert.deepEqual(await inaudible(), [false, false, false], "Multiple solos remain audible together");
            await toggle(1, 2, false);
            assert.deepEqual(await inaudible(), [false, true, false]);
            assert.equal(await evaluate('document.querySelector(".track-select[aria-pressed=true]").dataset.track'), "0",
                "Mute/solo must not select another plugin chain");
            const geometry = () => evaluate(`(() => {
                const sheet = document.querySelector("#sheet").getBoundingClientRect();
                const timeline = document.querySelector("#timeline").getBoundingClientRect();
                const plugins = document.querySelector("#plugins").getBoundingClientRect();
                return {top: plugins.top - sheet.top, bottom: plugins.bottom - timeline.bottom,
                    gap: plugins.left - timeline.right, sidebar: plugins.height, timeline: timeline.height};
            })()`);
            const beforeResize = await geometry();
            assert(Math.abs(beforeResize.top) < 1 && Math.abs(beforeResize.bottom) < 1 && beforeResize.gap >= 0,
                "The plugin sidebar spans the editor and timeline without overlapping either");
            await evaluate('document.querySelector("#split").dispatchEvent(new KeyboardEvent("keydown", {key: "ArrowUp"}))');
            await wait(`document.querySelector("#timeline").clientHeight > ${beforeResize.timeline}`);
            const afterResize = await geometry();
            assert.equal(afterResize.sidebar, beforeResize.sidebar, "Resizing the timeline preserves the full-height sidebar");
            assert.equal(await evaluate('document.querySelectorAll("#plugin-node,#plugin-kind").length'), 0);
            assert.equal(await evaluate(`document.querySelectorAll('button[aria-label="Toggle plugin window"]').length`), 2);
            assert.equal(await evaluate('document.querySelectorAll(".plugin").length'), 2);
            await openEffect();
            await wait(`${fixture}?.dataset.helper === "7"`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .2) < .0001`);
            // Moving a UI changes its frame, never its control subscription or instance.
            await evaluate(`globalThis.detachedUI = ${fixture}`);
            await click('.plugin[data-node="1"] button[aria-label="Toggle plugin window"]');
            const floating = `document.querySelector('.plugin-window[data-node="1"]')`;
            await wait(`${floating}?.open`);
            assert(await evaluate(`${floating}.querySelector('.plugin-body > div').shadowRoot.querySelector('.fixture-ui') === detachedUI`));
            const bounds = await evaluate(`${floating}.getBoundingClientRect().toJSON()`);
            const x = bounds.x + 30, y = bounds.y + 15;
            await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1});
            await call("Input.dispatchMouseEvent", {type: "mouseMoved", x: x + 80, y: y + 60, button: "left", buttons: 1});
            await call("Input.dispatchMouseEvent", {type: "mouseReleased", x: x + 80, y: y + 60, button: "left", clickCount: 1});
            await wait(`${floating}.getBoundingClientRect().x > ${bounds.x + 70}`);
            await click("#views");
            assert(await evaluate(`${floating}.open && detachedUI.isConnected`), "Hiding the sidebar leaves detached plugins open");
            await evaluate(`detachedUI.getRootNode().querySelector('#set-gain').click()`);
            await wait('Math.abs(Number(detachedUI.dataset.gain) - .3) < .0001');
            await evaluate(`document.querySelector('#track-list [data-track="1"]').click()`);
            assert(await evaluate(`${floating}.open && detachedUI.isConnected`), "Detached plugins survive chain selection");
            await evaluate(`document.querySelector('#track-list [data-track="0"]').click()`);
            await wait(`${synthRoot}?.querySelector('.parameter-controls')`);
            await evaluate(`document.querySelector('.plugin[data-node="0"] button[aria-label="Toggle plugin window"]').click()`);
            await wait('document.querySelectorAll(".plugin-window").length === 2');
            await click('.plugin-window[data-node="0"] button[aria-label="Close window"]');
            await click('.plugin-window[data-node="1"] button[aria-label="Close window"]');
            await wait(`${fixture} === detachedUI`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin-window").length'), 0);
            assert.equal(await evaluate('globalThis.fixtureFreed || 0'), 0, "Detach/attach never frees the custom UI");
            await evaluate('fixtureCallbacks.at(-1).set_parameter(1, .2)');
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .2) < .0001`);
            assert.equal(await evaluate(`getComputedStyle(${fixture}).display`), "grid");
            await evaluate(`${fixture}.style.setProperty('width', '720px')`);
            await wait(`(() => { const body = document.querySelector('.plugin[data-node="1"] .plugin-body');
                return ${fixture}.getBoundingClientRect().width < body.clientWidth && body.scrollWidth <= body.clientWidth; })()`);
            assert.equal(await evaluate('document.querySelector("#plugins").getBoundingClientRect().width'), 360,
                "A wide custom UI fits inside the fixed sidebar without horizontal scrolling");
            const scaledButton = await evaluate(`${fixture}.querySelector('button').getBoundingClientRect().toJSON()`);
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, x: scaledButton.right - 8, y: scaledButton.y + scaledButton.height / 2, button: "left", clickCount: 1});
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .3) < .0001`);
            assert.equal(await evaluate(`getComputedStyle(${fixture}.querySelector('button')).userSelect`), "none");
            await evaluate(`${fixture}.style.removeProperty('width')`);
            await wait(`${root}.querySelector('style').nextElementSibling.style.transform === ''`);
            assert(await evaluate(`${fixture} === detachedUI`), "Resizing retains the UI instance");
            await evaluate('fixtureCallbacks.at(-1).set_parameter(1, .2)');
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .2) < .0001`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 2, "Expanded plugins stay attached together");
            await evaluate(`globalThis.selectedChainUI = ${fixture}`);
            await click('.routing-node[data-node="1"]');
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin"), p => Number(p.dataset.node))'), [0, 1, 5],
                "Node selection follows the synth and effect to the master mix, without the other synth");
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin[open]"), p => Number(p.dataset.node))'), [1],
                "Only the selected node expands");
            assert(await evaluate(`${fixture} === selectedChainUI`), "Selecting the effect preserves its UI");
            await evaluate(`document.querySelector('.plugin[data-node="0"]').open = true`);
            await wait(`${synthRoot}?.querySelectorAll('.parameter-controls label').length === 3`);
            await evaluate(`(() => {
                const gain = ${synthRoot}.querySelectorAll("input")[0]; gain.value = .03;
                gain.dispatchEvent(new Event("input")); gain.dispatchEvent(new Event("change"));
            })()`);
            await wait(`${synthRoot}.querySelectorAll("output")[1].textContent.startsWith("0.03")`);
            assert(Math.abs(await evaluate(`Number(${fixture}.dataset.gain)`) - .2) < .0001);

            await evaluate(`${root}.querySelector('#set-gain').click()`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .3) < .0001`);
            await wait(`Math.abs(Number(${fixture}.dataset.meter) - .3) < .0001`);
            // Rapid gestures must converge without building hundreds of serialized host requests.
            await evaluate(`(() => { const callbacks = fixtureCallbacks.at(-1);
                for (let i = 1; i <= 400; ++i) callbacks.set_parameter(1, i / 1000);
            })()`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .4) < .0001`);
            await evaluate(`${root}.querySelector('#set-gain').click()`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .3) < .0001`);
            await evaluate(`${root}.querySelector('#send-message').click()`);
            await wait(`${root}.querySelector('output').textContent === "0,127,255"`);
            await toggleParameters();
            await wait(`${root}?.querySelectorAll(".parameter-controls label").length === 3`);
            assert.equal(await evaluate("fixtureFreed"), 1);
            await toggleParameters();
            await wait(`${fixture}?.dataset.helper === "7"`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .3) < .0001`);
            await evaluate("fixtureCallbacks[0].set_parameter(1, .9); fixtureCallbacks[0].msg_write(new Uint8Array([42]))");
            await new Promise(resolve => setTimeout(resolve, 150));
            assert(Math.abs(await evaluate(`Number(${fixture}.dataset.gain)`) - .3) < .0001, "Freed callbacks must not edit the DSP");
            await writeFile(`build/test/editor-ui-${mode}.png`, Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            const revision = await evaluate('document.querySelector("#timeline").dataset.revision');
            await evaluate(`globalThis.retainedUI = ${fixture}`);
            await click('#play[aria-label="Stop"]');
            await wait('document.querySelector("#play").getAttribute("aria-label") === "Play"');
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 2);
            assert.equal(await evaluate("fixtureFreed"), 1);
            await evaluate("fixtureCallbacks.at(-1).set_parameter(1, .6)");
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .6) < .0001`);
            await wait(`Math.abs(Number(${fixture}.dataset.meter) - .6) < .0001`);
            await click("#play");
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .6) < .0001`);
            await click("#rewind");
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .4) < .0001`);
            assert.equal(await evaluate(`document.querySelector('#track-list [data-listen="2"]').getAttribute('aria-pressed')`), "true");
            assert.deepEqual(await inaudible(), [false, true, false], "Stop/Play preserves audition state");
            assert.equal(await evaluate('document.querySelector("#timeline").dataset.revision'), revision);
            assert(await evaluate(`${fixture} === retainedUI`), "Play preserves the GUI object");
            assert.equal(await evaluate("fixtureFreed"), 1);
            await wait('!document.querySelector("#play").disabled');
            await key("Enter", "Enter", 2);
            await wait(`document.querySelector("#timeline").dataset.revision !== ${JSON.stringify(revision)} && !document.querySelector("#apply").disabled`);
            await wait(`${synthRoot}?.querySelector(".parameter-controls")`);
            assert.equal(await evaluate('document.querySelectorAll("#track-list [data-listen][aria-pressed=true]").length'), 0,
                "A newly prepared score clears mute/solo");
            assert.deepEqual(await inaudible(), [false, false, false]);
            const trackCommand = async args => {
                for (let i = 0; i < 30; ++i) {
                    const response = await evaluate(`import(${JSON.stringify(mode === "web" ? "../web/editor.js" : "./native.js")})
                        .then(adapter => adapter.command("listen", ...${JSON.stringify(args)}))`);
                    if (!response.error?.includes("busy")) return response;
                    await new Promise(resolve => setTimeout(resolve, 20));
                }
                throw Error("Track command remained busy");
            };
            assert.match((await trackCommand([Number(revision), 0, 1])).error, /Stale track view/);
            await wait(`Number(document.querySelector("#timeline").dataset.revision) > ${Number(revision)}`);
            const current = Number(await evaluate('document.querySelector("#timeline").dataset.revision'));
            for (const [track, flags] of [[-1, 0], [2, 0], [0.5, 0], [0, 4], [0, 0.5]])
                assert.match((await trackCommand([current, track, flags])).error, /Invalid track state/);
            await openEffect();
            await wait(`${fixture}?.dataset.helper === "7"`);
            await click("#views");
            await wait('document.querySelectorAll(".plugin-body > div").length === 0');
            assert.equal(await evaluate("fixtureFreed"), 3);
            await click("#views");
            await wait(`${fixture}?.dataset.helper === "7"`);
            // Creation-time gestures and replies survive an asynchronous factory spanning several polls.
            await wait('parseFloat(document.querySelector("#time").value) > 2');
            await toggleParameters();
            await wait(`${synthRoot}?.querySelector(".parameter-controls")`);
            await evaluate("fixtureWait = true; fixtureResume = undefined");
            await toggleParameters();
            await wait('typeof fixtureResume === "function"');
            await new Promise(resolve => setTimeout(resolve, 400));
            await evaluate("fixtureWait = false; fixtureResume()");
            await wait(`${root}?.querySelector('output')?.textContent === "13,0,255"`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .45) < .0001`);
            // Bad callbacks detach the view; audio continues and a new view can attach cleanly.
            await evaluate("fixtureCallbacks.at(-1).set_parameter(0, .9)");
            await wait(`${root} === undefined`);
            assert(await evaluate(`${synthRoot}?.querySelector(".parameter-controls")`));
            assert.match(await evaluate('document.querySelector("#errors").textContent'), /Invalid UI parameter/);
            assert.equal(await evaluate('document.querySelector("#play").getAttribute("aria-label")'), "Stop");
            await toggleParameters();
            await wait(`${root}?.querySelector(".parameter-controls")`);
            await toggleParameters();
            await wait(`${fixture}?.dataset.helper === "7"`);
            // Track headers select their actual chain and open the panel; other chains have no mounted views.
            await click("#views");
            await evaluate(`document.querySelector('#track-list [data-track="1"]').click()`);
            await wait('!document.querySelector("#plugins").hidden && document.querySelector("#views").checked');
            assert.equal(await evaluate('document.querySelectorAll(".plugin").length'), 1);
            await wait(`document.querySelector('.plugin[data-node="3"] .plugin-body > div')?.shadowRoot?.querySelector(".parameter-controls")`);
            await evaluate(`document.querySelector('#track-list [data-track="0"]').click()`);
            await wait(`${synthRoot}?.querySelector(".parameter-controls")`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin").length'), 2);
            await openEffect();
            await wait(`${fixture}?.dataset.helper === "7"`);
            await evaluate(`document.querySelector('.plugin[data-node="0"]').open = false`);
            await wait(`${synthRoot} === undefined`);
            assert(await evaluate(`${root}?.querySelector(".fixture-ui")`), "Collapsing one plugin preserves the other");
            await click('#play[aria-label="Stop"]');
            await wait('document.querySelector("#play").getAttribute("aria-label") === "Play"');
            // An asynchronous factory can complete while stopped: the prepared project still owns it.
            const freed = await evaluate("fixtureFreed");
            await wait('!document.querySelector("#play").disabled');
            await key("Enter", "Enter", 2);
            await wait(`${synthRoot}?.querySelector(".parameter-controls")`);
            await click("#play");
            await evaluate("fixtureWait = true; fixtureResume = undefined");
            await openEffect();
            await wait('typeof fixtureResume === "function"');
            await click('#play[aria-label="Stop"]');
            await wait('document.querySelector("#play").getAttribute("aria-label") === "Play"');
            await evaluate("fixtureWait = false; fixtureResume()");
            await wait(`fixtureFreed === ${freed + 1}`);
            await wait(`${fixture}?.dataset.helper === "7"`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .45) < .0001`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 2);
            // Collapsing still invalidates a factory whose returned UI arrives later.
            await toggleParameters();
            await wait(`${root}?.querySelector(".parameter-controls")`);
            const beforePending = await evaluate("fixtureFreed");
            await evaluate("fixtureWait = true; fixtureResume = undefined");
            await toggleParameters();
            await wait('typeof fixtureResume === "function"');
            await evaluate(`document.querySelector('.plugin[data-node="1"]').open = false`);
            await wait(`${root} === undefined`);
            await evaluate("fixtureWait = false; fixtureResume()");
            await wait(`fixtureFreed === ${beforePending + 1}`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 1);
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            // The same controls follow nested routing on both backends.
            const saved = await readFile(entry, "utf8");
            const graph = saved.replace('(daw/output (daw/master (daw/mix [a b])))', `
(def group (daw/track (daw/mix [a b]) {:name "Group"}))
(def wet (daw/track (daw/through group (daw/plugin "${directory}/effect.perone")) {:name "Wet"}))
(daw/output (daw/mix [group wet]))`);
            await set("#code", graph);
            await click("#play");
            await wait('document.querySelectorAll("#track-list [data-listen]").length === 8');
            assert.match(await evaluate('document.querySelector("#track-list .track:nth-child(3)").textContent'), /Group/);
            assert.match(await evaluate('document.querySelector("#track-list .track:nth-child(3) .track-select").title'), /Wet.*out|out.*Wet/);
            await toggle(3, 2, true);
            assert.deepEqual(await inaudible(), [false, false, false, false], "Wet solo retains upstream tracks");
            await toggle(3, 2, false);
            await toggle(0, 2, true);
            assert.deepEqual(await inaudible(), [false, true, false, false], "Source solo retains its group and effects");
            await evaluate(`document.querySelector('#track-list [data-track="2"]').click()`);
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin"), p => Number(p.dataset.node))'), [5],
                "The group exposes its explicit mix, without the plugins owned by its input tracks");
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 0);
            await evaluate(`document.querySelector('#track-list [data-track="3"]').click()`);
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin"), p => Number(p.dataset.node))'), [7]);
            await click('#play[aria-label="Stop"]');
            await wait('document.querySelector("#play").getAttribute("aria-label") === "Play"');
            const wet = `document.querySelector('.plugin[data-node="7"] .plugin-body > div')?.shadowRoot.querySelector('.fixture-ui')`;
            await wait(wet);
            const beforeClose = await evaluate('fixtureFreed');
            await click('.plugin[data-node="7"] button[aria-label="Toggle plugin window"]');
            await evaluate(`document.querySelector('#track-list [data-track="2"]').click()`);
            await click('.plugin-window button[aria-label="Close window"]');
            await wait(`fixtureFreed === ${beforeClose + 1}`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 0,
                "Closing a detached plugin outside the selected chain releases its UI");
            await evaluate(`document.querySelector('#track-list [data-track="3"]').click()`);
            await wait(wet);
            await click('.plugin[data-node="7"] button[aria-label="Toggle plugin window"]');
            await key("Enter", "Enter", 2);
            await wait('!document.querySelector(".plugin-window") && !document.querySelector("#apply").disabled');
            assert(await evaluate(`fixtureFreed >= ${beforeClose + 2}`), "Replacing a score disposes detached views");
            assert.deepEqual(diagnostics, []);
            // Restore the saved text before leaving, so the draft confirmation is not part of this test.
            await set("#code", saved);
            await call("Page.navigate", {url: "about:blank"});
            console.log(`OK: ${mode} DSP, custom/generic UI, floating windows, UI Wasm, automation, edits, messages, selection, stop and restart`);
        }, {graphics: true});
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
