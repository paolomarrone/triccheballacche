import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, readFile, readdir, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

const names = ["fx_svf", "fx_eq_3band", "synth_mono"];
names.push(...(await readdir("build/library/brickworks")).filter(name => /^(fx_|synth_)/.test(name) && !names.includes(name)).sort());
assert.equal(names.length, 40, "Build the complete Brickworks C library before testing its UIs");
const bundles = names.map(name => `build/library/brickworks/${name}/build/bw_example_${name}.perone`);
const directory = await mkdtemp("build/test/brickworks-ui-"), server = serve(0);
await once(server, "listening");
const origin = `http://127.0.0.1:${server.address().port}`;
let app;
try {
    await writeFile(`${directory}/preview.html`, `<!doctype html><meta charset="utf-8">
<style>body { margin:24px; background:#111714 } #host { width:760px; max-width:100% }</style><div id="host"></div>`);
    await withBrowser(async ({call, evaluate, wait, key, diagnostics}) => {
        const screenshot = async path => {
            const {x, y, width, height} = await evaluate("document.querySelector('.bw-panel').getBoundingClientRect().toJSON()");
            await writeFile(path, Buffer.from((await call("Page.captureScreenshot", {
                captureBeyondViewport: true, clip: {x, y, width, height, scale: 1}
            })).data, "base64"));
        };
        await call("Page.navigate", {url: `${origin}/${directory}/preview.html`});
        await call("Emulation.setDeviceMetricsOverride", {width: 1000, height: 1200, deviceScaleFactor: 1, mobile: false});
        for (const [i, bundle] of bundles.entries()) {
            const {product} = JSON.parse(await readFile(`${bundle}/product.json`, "utf8"));
            await evaluate(`(async () => {
                const {create} = await import('/${bundle}/' + ${JSON.stringify(product.ui.web)});
                const {fit} = await import('/editor/plugin-layout.js');
                globalThis.messages = [];
                const callbacks = {product: ${JSON.stringify(product)}};
                for (const kind of ['begin', 'end', 'write']) callbacks[kind === 'write' ? 'set_parameter' : 'set_parameter_' + kind] =
                    (index, value) => messages.push({kind, index, value});
                const surface = document.createElement('div'); document.querySelector('#host').append(surface);
                globalThis.ui = await create(surface, callbacks);
                globalThis.unfit = fit(document.querySelector('#host'), surface);
            })()`);
            assert.deepEqual((await evaluate('Array.from(document.querySelectorAll("[data-parameter]"), n => n.dataset.parameter)')).sort(),
                product.parameters.filter(p => !p.hidden).map(p => p.id).sort(), "Every visible DSP parameter has a control");
            await evaluate(`ui.set_parameter(0, ${product.parameters[0].minimum})`);
            assert.equal(await evaluate("messages.length"), 0, "DSP feedback must not echo back into the host");
            await evaluate(`ui.set_parameter(0, ${product.parameters[0].defaultValue})`);
            await screenshot(`build/test/${names[i]}-ui.png`);
            for (const width of [346, 280, 760]) {
                await evaluate(`document.querySelector('#host').style.width = '${width}px'`);
                await wait(`document.querySelector('.bw-panel').getBoundingClientRect().width <= ${width + 1}`);
                assert(await evaluate(`document.querySelector('#host').firstElementChild.style.transform === ''`),
                    "The layout reflows in a sidebar instead of shrinking its controls");
                assert(await evaluate(`Array.from(document.querySelectorAll('.bw-module')).every(n => n.scrollWidth <= n.clientWidth + 1)`),
                    "Controls fit their modules at narrow widths");
                if (width === 346) await screenshot(`build/test/${names[i]}-narrow-ui.png`);
            }
            if (i === 0) {
                const input = '[data-parameter="cutoff"] .bw-range';
                const rect = await evaluate(`document.querySelector('${input}').getBoundingClientRect().toJSON()`);
                const x = rect.x + rect.width / 2, y = rect.y + rect.height / 2;
                await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1});
                await call("Input.dispatchMouseEvent", {type: "mouseMoved", x, y: y - 32, button: "left", buttons: 1});
                await call("Input.dispatchMouseEvent", {type: "mouseReleased", x, y: y - 32, button: "left", clickCount: 1});
                let messages = await evaluate("messages.splice(0)");
                assert.equal(messages[0].kind, "begin"); assert.equal(messages.at(-1).kind, "end");
                assert(Math.abs(messages.at(-1).value - 1000 * Math.pow(1000, .2)) < .01, "The dial uses the logarithmic range");
                await evaluate("ui.set_parameter(0, 1000)");
                await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1, modifiers: 8});
                await call("Input.dispatchMouseEvent", {type: "mouseMoved", x, y: y - 32, button: "left", buttons: 1, modifiers: 8});
                await call("Input.dispatchMouseEvent", {type: "mouseReleased", x, y: y - 32, button: "left", clickCount: 1});
                messages = await evaluate("messages.splice(0)");
                assert(Math.abs(messages.at(-1).value - 1000 * Math.pow(1000, .02)) < .01, "Shift makes the same drag ten times finer");
                const beforeKey = messages.at(-1).value;
                await key("ArrowLeft");
                messages = await evaluate("messages.splice(0)");
                assert.equal(messages[0].kind, "begin"); assert.equal(messages.at(-1).kind, "end");
                assert(Math.abs(messages.at(-1).value - beforeKey / Math.pow(1000, .005)) < .01,
                    "Keyboard edits start at the exact value, without snapping to a range-input step");
                const value = '[data-parameter="cutoff"] input.bw-value';
                await evaluate(`document.querySelector('${value}').focus(); document.querySelector('${value}').value = '2300';
                    document.querySelector('${value}').dispatchEvent(new Event('input'))`);
                await key("Enter");
                assert.equal((await evaluate("messages.splice(0)")).at(-1).value, 2300);
                await evaluate(`document.querySelector('${value}').focus(); ui.set_parameter(0, 3200)`);
                await key("Escape");
                assert.equal(await evaluate("messages.length"), 0, "Cancelling an edit preserves concurrent DSP feedback");
                await evaluate(`document.querySelector('${value}').focus(); ui.set_parameter(0, 4200); document.querySelector('${value}').blur()`);
                assert.equal(await evaluate("messages.length"), 0, "Focusing a value without editing does not write stale data");
                await evaluate(`document.querySelector('${input}').dispatchEvent(new MouseEvent('dblclick'))`);
                assert.equal((await evaluate("messages.splice(0)")).at(-1).value, 1000);
                await evaluate("document.querySelector('.bw-point').focus()");
                await key("ArrowUp");
                assert.equal((await evaluate("messages.splice(0)")).at(-1).index, 1, "The XY pad edits Q independently");
                await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1});
            }
            if (i === 2) {
                await evaluate(`document.querySelector('[data-parameter="vco1_wave"] button:nth-child(3)').click()`);
                assert.equal((await evaluate("messages.splice(0)")).at(-1).value, 3, "Choices use metadata values");
                const a440 = '[data-parameter="a440"] button';
                assert.equal(await evaluate(`document.querySelectorAll('${a440}').length`), 1);
                await evaluate(`document.querySelector('${a440}').focus()`);
                for (const value of [1, 0]) {
                    await key(" ", "Space");
                    const messages = await evaluate("messages.splice(0)");
                    assert.deepEqual(messages.map(m => m.kind), ["begin", "write", "end"]);
                    assert.equal(messages.at(-1).value, value, "The same A440 button turns the tone on and off");
                    assert.equal(await evaluate(`document.querySelector('${a440}').getAttribute('aria-pressed')`), String(!!value));
                }
                await evaluate(`ui.set_parameter(${product.parameters.findIndex(p => p.id === "a440")}, 1)`);
                assert.equal(await evaluate(`document.querySelector('${a440}').getAttribute('aria-pressed')`), "true");
                assert.equal(await evaluate("messages.length"), 0, "The toggle follows DSP feedback without echoing it");
                await evaluate('ui.set_parameter(38, -12)');
                assert.equal(await evaluate("document.querySelector('meter').value"), -12);
            }
            await evaluate('unfit(); ui.free()');
            if (i === 0) {
                assert.deepEqual((await evaluate("messages.splice(0)")).map(m => m.kind), ["begin", "end"],
                    "Disposing a UI completes an active gesture");
                await call("Input.dispatchMouseEvent", {type: "mouseReleased", x: 0, y: 0, button: "left", clickCount: 1});
            }
            assert.equal(await evaluate('document.querySelectorAll(".bw-panel, link").length'), 0);
            await evaluate('document.querySelector("#host").replaceChildren()');
        }
        assert.deepEqual(diagnostics, []);
    });
    console.log(`OK: all ${names.length} Brickworks UIs, gestures, keyboard, numeric editing, feedback, meters and responsive layouts`);

    const entry = `${directory}/score.janet`;
    await writeFile(entry, `(def synth (daw/plugin "${bundles[2]}" {:vcf_cutoff 1200 :vca_release 180}))
(def filter (daw/plugin "${bundles[0]}" {:cutoff 4200}))
(def eq (daw/plugin "${bundles[1]}" {:ls_gain 3 :peak_gain -4}))
(daw/output (daw/track synth {:effects [filter eq] :gain 0.2}))
(daw/note synth 0 5 48 100)
(daw/param filter 0.5 :cutoff 800)
(daw/end 30)
`);
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", "examples/controls.janet", directory, ...bundles]);
    for (const mode of ["web", "native"]) {
        let url = `${origin}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") { app = nativeEditor(entry); url = await app.url; }
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            await call("Emulation.setDeviceMetricsOverride", {width: 1200, height: 900, deviceScaleFactor: 1, mobile: false});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#play")?.disabled === false');
            await click("#views"); await click("#play");
            const body = id => `document.querySelector('.plugin[data-node="${id}"] .plugin-body > div')?.shadowRoot`;
            await wait(`${body(0)}?.querySelectorAll('[data-parameter]').length === 39`);
            for (const id of [1, 2]) await evaluate(`document.querySelector('.plugin[data-node="${id}"]').open = true`);
            await wait(`${body(2)}?.querySelectorAll('[data-parameter]').length === 9`);
            await wait(`${body(1)}?.querySelector('[data-parameter="cutoff"] .bw-value').value === '800 Hz'`);
            await evaluate(`globalThis.filterUI = ${body(1)}.querySelector('.bw-panel')`);
            await click('.plugin[data-node="1"] button[aria-label="Toggle plugin window"]');
            await wait('document.querySelector(".plugin-window")?.open');
            assert(await evaluate('filterUI.isConnected'), "Detach retains the live UI");
            await evaluate(`filterUI.querySelector('[data-parameter="cutoff"] .bw-range').dispatchEvent(new MouseEvent('dblclick'))`);
            await wait('filterUI.querySelector(".bw-value").value === "1 kHz"');
            await click('.plugin-window button[aria-label="Close window"]');
            await wait(`${body(1)}?.querySelector('.bw-panel') === filterUI`);
            await evaluate(`document.querySelector('.plugin[data-node="1"] button[aria-label="Toggle parameter controls"]').click()`);
            await wait(`${body(1)}?.querySelector('.parameter-controls output').textContent === '1,000 Hz'`);
            await click("#play");
            await set("#path", "examples/controls.janet");
            await evaluate('document.querySelector("#path").focus()'); await key("Enter");
            await wait('!document.querySelector("#play").disabled && document.querySelector("#code").textContent.includes("Three Brickworks")');
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > .5');
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            assert.deepEqual(diagnostics, []);
        }, {graphics: true});
        if (app) { await app.close(); app = undefined; }
        console.log(`OK: Brickworks UIs with ${mode} DSP, automation and detach/attach`);
    }
} finally {
    if (app) await app.close();
    server.close();
    await rm(directory, {recursive: true, force: true});
}
