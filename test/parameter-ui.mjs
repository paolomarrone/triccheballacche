import assert from "node:assert/strict";
import {once} from "node:events";
import {mkdir, mkdtemp, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {pluginControls} from "../editor/plugin-controls.js";

// Preset capture must wait for accepted edits and abort if the backend rejects them.
{
    const failures = [], error = Error("Parameter write failed");
    let reject;
    const controls = pluginControls({parameters: [{direction: "input", minimum: 0, maximum: 1}]},
        () => new Promise((_, fail) => { reject = fail; }), failure => failures.push(failure));
    controls.attach({});
    controls.callbacks.set_parameter(0, .625);
    let settled = false;
    const pending = controls.flush();
    pending.then(() => { settled = true; }, () => { settled = true; });
    await Promise.resolve();
    assert(!settled, "Capture waits for the pending parameter write");
    reject(error);
    await assert.rejects(pending, error);
    assert.deepEqual(failures, [error]);
    controls.close();
    console.log("OK: preset capture waits for edits and propagates write failures");
}

// Repeated snapshots do not repaint unchanged controls, but local edits invalidate feedback.
{
    const values = [], messages = [];
    const controls = pluginControls({parameters: [{direction: "input", minimum: 0, maximum: 1}]},
        async () => ({}), error => { throw error; });
    controls.attach({set_parameter: (index, value) => values.push([index, value]), msg_in: bytes => messages.push([...bytes])});
    const read = async () => ({values: [.25], messages: [[1]]});
    assert(await controls.poll(read));
    assert(await controls.poll(read));
    assert.deepEqual(values, [[0, .25]]);
    assert.deepEqual(messages, [[1], [1]], "Messages are events, even if their payloads match");
    controls.callbacks.set_parameter(0, .75);
    await controls.flush();
    assert(await controls.poll(read));
    assert.deepEqual(values, [[0, .25], [0, .25]], "Fresh automation can restore the value from before a local edit");
    controls.close();
    console.log("OK: unchanged feedback is skipped without losing messages or post-edit automation");
}

// Shared UI behavior needs only the browser and metadata, without audio builds.
await mkdir("build/test", {recursive: true});
const directory = await mkdtemp("build/test/parameter-ui-"), server = serve(0);
await once(server, "listening");
try {
    // Hold feedback and edit acknowledgements independently to reproduce delayed
    // polling during a gesture, after release, and across a completed edit.
    await writeFile(`${directory}/controls.html`, '<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/editor/style.css"><aside id="plugins" style="width:360px;flex:none"><div id="plugin-list"></div></aside>');
    await withBrowser(async ({call, evaluate, wait, key}) => {
        await call("Emulation.setDeviceMetricsOverride", {width: 640, height: 520, deviceScaleFactor: 1, mobile: false});
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/${directory}/controls.html`});
        const result = await evaluate(`(async () => {
            const {plugins} = await import('/editor/plugins.js');
            const {product} = await (await fetch('/test/perone/product.json')).json();
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
                    const data = {controls: id.map(() => ({values: [...values], messages: []}))};
                    if (hold) return new Promise(resolve => replies.push(() => resolve(data)));
                    return data;
                }
                return {};
            };
            const host = plugins(request, {}, error => { if (error) throw error; }, visible => { document.querySelector('#plugins').hidden = !visible; });
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
        assert(await evaluate(`(() => {
            const button = document.querySelector('button[aria-label="Toggle plugin window"]'), label = button.firstChild;
            controlsTest.host.windows([]);
            controlsTest.host.windows([]);
            return button.firstChild === label;
        })()`), "Unchanged window status preserves the label so WebKit can complete a mouse click");
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

        await evaluate(`(async () => {
            const {plugins} = await import('/editor/plugins.js');
            const product = {parameters: [{id: 'gain', name: 'Gain', direction: 'input', minimum: 0, maximum: 1, defaultValue: 0}]};
            const requests = [], failures = [];
            let reply;
            const host = plugins(async (op, revision, ids) => {
                if (op !== 'controls') return {};
                requests.push(ids);
                if (globalThis.holdBatch) return new Promise(resolve => { reply = resolve; });
                return {controls: ids.map(id => ({values: [(id + 1) / 10], messages: []}))};
            }, {}, error => failures.push(error.message), () => {});
            host.status(true, false);
            host.score({revision: 1, nodes: [0, 1, 2].map(id => ({name: 'Plugin ' + id, product, inputs: id ? [id - 1] : []}))
                .concat({inputs: [2]}), tracks: [[0, 3, 1, 2]]});
            host.show(true);
            for (const details of document.querySelectorAll('.plugin')) details.open = true;
            globalThis.batchTest = {host, requests, failures, reply(data) { reply(data); }};
        })()`);
        await wait('Array.from(document.querySelectorAll(".plugin-body > div")).filter(host => !host.inert).length === 3');
        await evaluate('batchTest.requests.length = 0; holdBatch = true; globalThis.batchPending = batchTest.host.poll(); void 0');
        assert.deepEqual(await evaluate('batchTest.requests'), [[0, 1, 2]], "Open views share one snapshot request");
        await evaluate('document.querySelectorAll(".plugin")[1].open = false');
        await wait('document.querySelectorAll(".plugin-body > div").length === 2');
        await evaluate(`batchTest.reply({controls: [{values: [.8], messages: []}, {values: [.9], messages: []}, {error: 'Plugin disconnected'}]}); batchPending`);
        assert.deepEqual(await evaluate('batchTest.failures'), ["Plugin disconnected"]);
        assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 1, "Disposal and a failed view do not affect the others");
        assert.equal(await evaluate('document.querySelector(".plugin-body > div").shadowRoot.querySelector("input").value'), "0.8");
        await evaluate('holdBatch = false; batchTest.requests.length = 0; batchTest.host.poll()');
        assert.deepEqual(await evaluate('batchTest.requests'), [[0]], "Closed and failed views are absent from subsequent snapshots");
        await evaluate('batchTest.host.dispose(); batchTest.requests.length = 0; batchTest.host.poll()');
        assert.deepEqual(await evaluate('batchTest.requests'), [], "No requests without mounted views");
        console.log("OK: batched plugin feedback, disposal during reads and independent view failures");
    });
} finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    await rm(directory, {recursive: true, force: true});
}
