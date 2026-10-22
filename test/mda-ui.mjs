import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {nativeEditor} from "./native.mjs";
import {once} from "node:events";
import {readFile, readdir, writeFile} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const names = (await readdir("build/library/mda")).filter(n => n.endsWith(".perone")).sort();
assert.equal(names.length, 39);
let app;
const server = serve(0); await once(server, "listening");
try {
    await writeFile("build/test/mda-preview.html", '<!doctype html><meta charset="utf-8"><style>body{margin:20px;background:#12151b}#host{width:560px}</style><div id="host"></div>');
    await withBrowser(async ({call, evaluate, key, diagnostics}) => {
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/build/test/mda-preview.html`});
        await call("Emulation.setDeviceMetricsOverride", {width: 1000, height: 1400, deviceScaleFactor: 1, mobile: false});
        for (const name of names) {
            const bundle = `build/library/mda/${name}`, {product} = JSON.parse(await readFile(`${bundle}/product.json`, "utf8"));
            await evaluate(`(async () => {
                const {create} = await import('/${bundle}/${product.ui.web}');
                globalThis.events = [];
                globalThis.ui = await create(document.querySelector('#host'), {product:${JSON.stringify(product)},
                    set_parameter_begin:(i,v)=>events.push(['begin',i,v]), set_parameter:(i,v)=>events.push(['write',i,v]), set_parameter_end:(i,v)=>events.push(['end',i,v])});
            })()`);
            assert.deepEqual((await evaluate("Array.from(document.querySelectorAll('[data-parameter]'),e=>e.dataset.parameter)")).sort(), product.parameters.map(p => p.id).sort(), name);
            for (const width of [280, 346, 560]) {
                await evaluate(`document.querySelector('#host').style.width='${width}px'`);
                assert(await evaluate("Array.from(document.querySelectorAll('.mda-panel,.mda-module')).every(e=>e.scrollWidth<=e.clientWidth+1)"), `${name}: overflow at ${width}`);
                if (width !== 346) {
                    const clip = await evaluate("document.querySelector('.mda-panel').getBoundingClientRect().toJSON()");
                    const shot = await call("Page.captureScreenshot", {captureBeyondViewport: true, clip: {...clip, scale: 1}});
                    await writeFile(`build/test/${name}-${width}.png`, Buffer.from(shot.data, "base64"));
                }
            }
            await evaluate(`ui.set_parameter(0,${product.parameters[0].maximum})`);
            for (const [index, parameter] of product.parameters.entries()) {
                for (const [label, value] of Object.entries(parameter.scalePoints || {})) {
                    await evaluate(`ui.set_parameter(${index}, Math.fround(${value}))`);
                    assert.deepEqual(await evaluate(`Array.from(document.querySelectorAll('[data-parameter="${parameter.id}"] [aria-pressed="true"]'), e => e.textContent)`),
                        [label], `${name}: ${parameter.id} must select the DSP mode after float32 feedback`);
                }
            }
            const boundary = {"mdaBandisto.perone": [9, .501, "Bipolar"], "mdaLimiter.perone": [4, .5, "Hard"]}[name];
            if (boundary) {
                const [index, value, label] = boundary;
                await evaluate(`ui.set_parameter(${index}, ${value})`);
                assert.equal(await evaluate(`document.querySelector('[data-parameter="${product.parameters[index].id}"] [aria-pressed="true"]').textContent`), label);
            }
            assert.equal(await evaluate("events.length"), 0, "Feedback must not echo");
            if (await evaluate("!!document.querySelector('.mda-range')")) {
                await evaluate("document.querySelector('.mda-range').focus()");
                await key("ArrowLeft");
                assert.deepEqual((await evaluate("events.splice(0)")).map(e => e[0]), ["begin", "write", "end"]);
            }
            if (await evaluate("!!document.querySelector('.mda-point')")) {
                await evaluate("document.querySelector('.mda-point').focus()"); await key("ArrowUp");
                assert.deepEqual((await evaluate("events.splice(0)")).map(e => e[0]), ["begin", "write", "end"]);
            }
            if (await evaluate("!!document.querySelector('.mda-presets')")) {
                await evaluate("{ const select=document.querySelector('.mda-presets');select.value='1';select.dispatchEvent(new Event('change')); }");
                assert.equal((await evaluate("events.splice(0)")).filter(e => e[0] === "write").length, product.parameters.length);
            }
            await evaluate("ui.free()");
            assert.equal(await evaluate("document.querySelector('#host').children.length"), 0);
        }
        assert.deepEqual(diagnostics, []);
    });

    const entry = "build/test/mda-chain.janet", catalog = "build/test/mda-project.json";
    await writeFile(entry, `(def synth (daw/plugin "build/library/mda/mdaDX10.perone"))
(def drive (daw/plugin "build/library/mda/mdaOverdrive.perone" {:drive 0.15}))
(def meter (daw/plugin "build/library/mda/mdaSpecMeter.perone"))
(daw/output (daw/track synth {:effects [drive meter] :gain 0.1}))
(daw/note synth 0 15 48 100)
(daw/param drive 0.2 :drive 0.75)
(daw/end 30)
`);
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", entry, ...names.map(n => `build/library/mda/${n}`)]);
    for (const mode of ["web", "native"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") { app = nativeEditor(entry); url = await app.url; }
        await withBrowser(async ({call, evaluate, wait, click, diagnostics}) => {
            await call("Emulation.setDeviceMetricsOverride", {width: 1200, height: 900, deviceScaleFactor: 1, mobile: false});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#play")?.disabled === false');
            await click("#views"); await click("#play");
            const body = id => `document.querySelector('.plugin[data-node="${id}"] .plugin-body > div')?.shadowRoot`;
            await wait(`${body(0)}?.querySelector('.mda-panel')`);
            for (const id of [1, 2]) await evaluate(`document.querySelector('.plugin[data-node="${id}"]').open = true`);
            await wait(`${body(1)}?.querySelector('[data-parameter="drive"] .mda-value')?.value === '75.0%'`);
            await wait(`${body(2)}?.querySelector('[data-parameter="peak_l"] output')?.value &&
                parseFloat(${body(2)}.querySelector('[data-parameter="peak_l"] output').value) > -80`);
            await evaluate(`globalThis.driveUI = ${body(1)}.querySelector('.mda-panel')`);
            await click('.plugin[data-node="1"] button[aria-label="Toggle plugin window"]');
            await wait('document.querySelector(".plugin-window")?.open');
            assert(await evaluate('driveUI.isConnected'));
            await evaluate(`driveUI.querySelector('[data-parameter="drive"] .mda-range').dispatchEvent(new MouseEvent('dblclick'))`);
            await wait(`driveUI.querySelector('[data-parameter="drive"] .mda-value').value === '0.0%'`);
            await click('.plugin-window button[aria-label="Close window"]');
            await wait(`${body(1)}?.querySelector('.mda-panel') === driveUI`);
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            assert.deepEqual(diagnostics, []);
            await click("#play");
        }, {graphics: true});
        if (app) { await app.close(); app = undefined; }
        console.log(`OK: MDA UI with ${mode} DSP, automation, real meters and detach/attach`);
    }
} finally { if (app) await app.close(); server.close(); }

console.log("OK: all 39 MDA UIs, parameter coverage, narrow layouts, keyboard, feedback, presets and disposal");
