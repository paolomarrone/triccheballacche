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
    // Hold feedback and edit acknowledgements independently to reproduce delayed
    // polling during a gesture, after release, and across a completed edit.
    await writeFile(`${directory}/controls.html`, '<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/editor/style.css"><aside id="plugins" style="width:360px;flex:none"><div id="plugin-list"></div></aside>');
    await withBrowser(async ({call, evaluate, wait, key}) => {
        await call("Emulation.setDeviceMetricsOverride", {width: 640, height: 520, deviceScaleFactor: 1, mobile: false});
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/${directory}/controls.html`});
        const result = await evaluate(`(async () => {
            const {plugins} = await import('/editor/plugins.js');
            const {product} = await (await fetch('/build/test/fixture.perone/product.json')).json();
            product.parameters.push(
                {id: 'cutoff', name: 'Filter cutoff', direction: 'input', minimum: 20, maximum: 20000, defaultValue: 200, map: 'logarithmic', unit: 'hz'},
                {id: 'wave', name: 'Waveform', direction: 'input', minimum: 10, maximum: 90, defaultValue: 20, list: true, scalePoints: {Pulse: 90, Sine: 10, Saw: 20}},
                {id: 'bypass', name: 'Bypass', direction: 'input', minimum: 0, maximum: 1, defaultValue: 0, isBypass: true}
            );
            const values = [0, null, 0, 200, 20, 0], writes = [], replies = [], results = {};
            let hold = true;
            const request = async (op, revision, id, index, value) => {
                if (op === 'parameter') return new Promise(resolve => writes.push(() => { values[index] = value; resolve({}); }));
                if (op === 'controls') {
                    const data = {values: [...values], messages: []};
                    if (hold) return new Promise(resolve => replies.push(() => resolve(data)));
                    return data;
                }
                return {};
            };
            const host = plugins(request, {}, error => { if (error) throw error; });
            host.status(true, false);
            host.score({revision: 1, nodes: [{name: 'Synth', inputs: [], product}, {inputs: [0]}], tracks: [[0, 1]]});
            host.show(true);
            await new Promise(requestAnimationFrame);
            const root = document.querySelector('.plugin-body > div').shadowRoot;
            const gain = root.querySelector('input');
            const hidden = () => getComputedStyle(gain).visibility === 'hidden' && root.host.inert;
            results.initialHidden = hidden();
            gain.focus();
            results.initialUnfocused = root.activeElement !== gain;
            const move = value => { gain.value = value; gain.dispatchEvent(new Event('input')); };
            const end = () => gain.dispatchEvent(new Event('change'));
            const ack = async () => { writes.shift()(); await Promise.resolve(); await Promise.resolve(); };
            const feedback = async pending => { replies.shift()(); await pending; };
            await feedback();
            await new Promise(requestAnimationFrame);
            results.partialHidden = hidden();
            values[1] = .1;
            await feedback(host.poll());
            results.initialVisible = getComputedStyle(gain).visibility === 'visible' && !root.host.inert;
            results.initialValue = Number(gain.value);
            let poll = host.poll();
            gain.dispatchEvent(new PointerEvent('pointerdown'));
            for (let i = 1; i <= 80; ++i) move(i / 100);
            await feedback(poll);
            results.during = Number(gain.value);
            await ack(); await ack();
            results.queued = writes.length;
            values[0] = .6; values[1] = .3; values[2] = 2;
            await feedback(host.poll());
            results.held = Number(gain.value);
            results.meter = root.querySelector('meter').value;
            results.other = Number(root.querySelectorAll('input')[1].value);
            end();
            poll = host.poll();
            await ack();
            await feedback(poll);
            results.released = Number(gain.value);
            results.sent = values[1];
            values[1] = .25;
            await feedback(host.poll());
            results.resumed = Number(gain.value);
            poll = host.poll();
            move(.7); end();
            await ack(); await ack();
            await feedback(poll);
            results.completed = Number(gain.value);
            hold = false;
            globalThis.controlsTest = {host, root, values, async sync() {
                while (writes.length) await ack();
                await host.poll();
            }};
            return results;
        })()`);
        assert(result.initialHidden, "Default values stay hidden until the initial feedback arrives");
        assert(result.initialUnfocused, "Pending controls cannot receive keyboard focus");
        assert(result.partialHidden, "Missing values keep the view hidden until every parameter has been initialized");
        assert(result.initialVisible, "The synchronized view becomes visible and interactive");
        assert.equal(result.initialValue, .1, "The first visible value comes from the host, not the metadata default");
        console.log("OK: initial parameter synchronization hides defaults and waits for pending values");
        assert.equal(result.during, .8, "An older poll cannot overwrite the value being dragged");
        assert.equal(result.queued, 0, "Rapid edits coalesce to the latest value");
        assert.equal(result.held, .8, "Active gestures retain their local value after acknowledgements");
        assert.equal(result.meter, .6, "Output meters keep receiving feedback during a gesture");
        assert(Math.abs(result.other - 2 / 3) < 1e-12, "Other input parameters keep receiving feedback during a gesture");
        assert.equal(result.released, .8, "A poll started before acknowledgement cannot overwrite the released value");
        assert.equal(result.sent, .8, "Releasing the slider sends the final local value");
        assert.equal(result.resumed, .25, "Fresh DSP feedback resumes after the gesture");
        assert.equal(result.completed, .7, "A completed edit still invalidates an older poll");
        console.log("OK: parameter gestures, delayed feedback, acknowledgements, coalescing and independent controls");
        await wait('controlsTest.root.querySelector("link").sheet !== null');
        await evaluate('controlsTest.root.querySelector(".parameter-choice").focus()');
        await key("ArrowDown");
        await wait('controlsTest.root.querySelector(".parameter-choice").getAttribute("aria-expanded") === "true"');
        assert.deepEqual(await evaluate('Array.from(controlsTest.root.querySelectorAll("[role=option]"), option => option.textContent)'),
            ["Sine", "Saw", "Pulse"], "Choice order follows parameter values, independent of metadata key order");
        assert.equal(await evaluate('controlsTest.root.activeElement.textContent'), "Saw");
        await key("End");
        assert.equal(await evaluate('controlsTest.root.activeElement?.textContent'), "Pulse", "End focuses the last option");
        assert(await evaluate('controlsTest.root.querySelector(".parameter-menu").matches(":popover-open")'), "Keyboard navigation keeps the menu open");
        await key("Enter");
        await evaluate('controlsTest.sync()');
        assert.equal(await evaluate('controlsTest.values[4]'), 90, "Choice menus send the option value, not its position");
        assert.equal(await evaluate('controlsTest.root.querySelector(".parameter-choice").textContent'), "Pulse");
        await key("ArrowDown");
        await wait('controlsTest.root.activeElement?.getAttribute("role") === "option"');
        await key("s", "KeyS");
        assert.equal(await evaluate('controlsTest.root.activeElement.textContent'), "Sine", "Choice menus support type-ahead");
        await key("Escape");
        assert.equal(await evaluate('controlsTest.root.querySelector(".parameter-choice").value'), "90", "Escape cancels a pending choice");
        await evaluate('controlsTest.root.querySelector("input[type=checkbox]").focus()');
        await key(" ", "Space");
        await evaluate('controlsTest.sync()');
        assert.equal(await evaluate('controlsTest.values[5]'), 1, "Switches work from the keyboard");
        await evaluate(`(() => {
            const cutoff = controlsTest.root.querySelectorAll('input[type=range]')[2];
            cutoff.value = .5; cutoff.dispatchEvent(new Event('input')); cutoff.dispatchEvent(new Event('change'));
        })()`);
        await evaluate('controlsTest.sync()');
        assert(Math.abs(await evaluate('controlsTest.values[3]') - Math.sqrt(20 * 20000)) < 1e-6,
            "Styled sliders preserve logarithmic parameter mapping");
        for (const theme of ["light", "dark"]) {
            await call("Emulation.setEmulatedMedia", {features: [{name: "prefers-color-scheme", value: theme}]});
            await evaluate('controlsTest.root.querySelector(".parameter-choice").click()');
            await wait('controlsTest.root.querySelector(".parameter-menu").matches(":popover-open")');
            const shot = await call("Page.captureScreenshot", {format: "png"});
            await writeFile(`build/test/parameter-controls-${theme}.png`, Buffer.from(shot.data, "base64"));
            await key("Escape");
        }
        await evaluate('controlsTest.host.dispose()');
        assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 0);
        console.log("OK: styled parameter controls, keyboard menus, switches, logarithmic mapping and disposal");
    });
    for (const mode of ["web", "native"]) {
        let url = wasm;
        if (mode === "native") {
            app = nativeEditor(entry);
            url = await app.url;
        }
        await withBrowser(async ({call, evaluate, wait, click, key, diagnostics}) => {
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
            await wait(`${synthRoot}?.querySelectorAll(".perone-controls label").length === 3`);
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
            assert.equal(await evaluate(`document.querySelectorAll('button[aria-label="Toggle native UI"]').length`), 0);
            assert.equal(await evaluate('document.querySelectorAll(".plugin").length'), 2);
            await openEffect();
            await wait(`${fixture}?.dataset.helper === "7"`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .2) < .0001`);
            assert.equal(await evaluate(`getComputedStyle(${fixture}).display`), "grid");
            await evaluate(`${fixture}.style.setProperty('width', '720px')`);
            assert.equal(await evaluate('document.querySelector("#plugins").getBoundingClientRect().width'), 360,
                "A wide custom UI scrolls inside the fixed sidebar");
            assert(await evaluate(`(() => { const body = document.querySelector('.plugin[data-node="1"] .plugin-body'); return body.scrollWidth > body.clientWidth; })()`));
            assert.equal(await evaluate(`getComputedStyle(${fixture}.querySelector('button')).userSelect`), "none");
            await evaluate(`${fixture}.style.removeProperty('width')`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 2, "Expanded plugins stay attached together");
            await evaluate(`globalThis.selectedChainUI = ${fixture}`);
            await click('.routing-node[data-node="1"]');
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin"), p => Number(p.dataset.node))'), [0, 1, 5],
                "Node selection follows the synth and effect to the master mix, without the other synth");
            assert.deepEqual(await evaluate('Array.from(document.querySelectorAll(".plugin[open]"), p => Number(p.dataset.node))'), [1],
                "Only the selected node expands");
            assert(await evaluate(`${fixture} === selectedChainUI`), "Selecting the effect preserves its UI");
            await evaluate(`document.querySelector('.plugin[data-node="0"]').open = true`);
            await wait(`${synthRoot}?.querySelectorAll('.perone-controls label').length === 3`);
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
            await wait(`${root}?.querySelectorAll(".perone-controls label").length === 3`);
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
            await click("#stop");
            await wait('document.querySelector("#stop").disabled');
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
            await wait(`${synthRoot}?.querySelector(".perone-controls")`);
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
            await wait(`${synthRoot}?.querySelector(".perone-controls")`);
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
            assert(await evaluate(`${synthRoot}?.querySelector(".perone-controls")`));
            assert.match(await evaluate('document.querySelector("#errors").textContent'), /Invalid UI parameter/);
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false);
            await toggleParameters();
            await wait(`${root}?.querySelector(".perone-controls")`);
            await toggleParameters();
            await wait(`${fixture}?.dataset.helper === "7"`);
            // Track headers select their actual chain and open the panel; other chains have no mounted views.
            await click("#views");
            await evaluate(`document.querySelector('#track-list [data-track="1"]').click()`);
            await wait('!document.querySelector("#plugins").hidden && document.querySelector("#views").checked');
            assert.equal(await evaluate('document.querySelectorAll(".plugin").length'), 1);
            await wait(`document.querySelector('.plugin[data-node="3"] .plugin-body > div')?.shadowRoot?.querySelector(".perone-controls")`);
            await evaluate(`document.querySelector('#track-list [data-track="0"]').click()`);
            await wait(`${synthRoot}?.querySelector(".perone-controls")`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin").length'), 2);
            await openEffect();
            await wait(`${fixture}?.dataset.helper === "7"`);
            await evaluate(`document.querySelector('.plugin[data-node="0"]').open = false`);
            await wait(`${synthRoot} === undefined`);
            assert(await evaluate(`${root}?.querySelector(".fixture-ui")`), "Collapsing one plugin preserves the other");
            await click("#stop");
            await wait('document.querySelector("#stop").disabled');
            // An asynchronous factory can complete while stopped: the prepared project still owns it.
            const freed = await evaluate("fixtureFreed");
            await wait('!document.querySelector("#play").disabled');
            await key("Enter", "Enter", 2);
            await wait(`${synthRoot}?.querySelector(".perone-controls")`);
            await evaluate("fixtureWait = true; fixtureResume = undefined");
            await openEffect();
            await wait('typeof fixtureResume === "function"');
            await click("#stop");
            await wait('document.querySelector("#stop").disabled');
            await evaluate("fixtureWait = false; fixtureResume()");
            await wait(`fixtureFreed === ${freed + 1}`);
            await wait(`${fixture}?.dataset.helper === "7"`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .45) < .0001`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 2);
            // Collapsing still invalidates a factory whose returned UI arrives later.
            await toggleParameters();
            await wait(`${root}?.querySelector(".perone-controls")`);
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
            await evaluate(`(() => { const code = document.querySelector('#code'); code.value = ${JSON.stringify(graph)}; code.dispatchEvent(new Event('input')); })()`);
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
            await click("#stop");
            await wait('document.querySelector("#stop").disabled');
            assert.deepEqual(diagnostics, []);
            // Restore the saved text before leaving, so the draft confirmation is not part of this test.
            await evaluate(`(() => { const code = document.querySelector('#code'); code.value = ${JSON.stringify(saved)}; code.dispatchEvent(new Event('input')); })()`);
            await call("Page.navigate", {url: "about:blank"});
            console.log(`OK: ${mode} DSP, shared custom/generic UI, relative JS/CSS/UI Wasm, automation, edits, messages, selection, stop and restart`);
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
