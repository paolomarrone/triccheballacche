import assert from "node:assert/strict";
import {spawn, execFileSync} from "node:child_process";
import {once} from "node:events";
import {cp, mkdtemp, readFile, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const directory = await mkdtemp("build/editor-ui-"), entry = `${directory}/score.janet`;
let server, app;
try {
    for (const [from, to] of [["fixture", "synth"], ["effect", "effect"]]) {
        const bundle = `${directory}/${to}.perone`;
        await cp(`build/${from}.perone`, bundle, {recursive: true});
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
(daw/track s {:effects [f]})
(daw/track (daw/plugin "${directory}/synth.perone" {:gain 0.01}))
(daw/param f 1.5 :gain 0.2)
(daw/end 40)
`);
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", directory]);
    server = serve(0);
    await once(server, "listening");
    const wasm = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
    for (const mode of ["web", "native"]) {
        let url = wasm, errors = "";
        if (mode === "native") {
            app = spawn("./build/editor", ["--serve", entry]);
            app.stderr.on("data", data => errors += data);
            url = await new Promise((resolve, reject) => {
                let text = "";
                app.on("error", reject);
                app.on("exit", () => reject(Error(errors)));
                app.stdout.on("data", data => {
                    text += data;
                    const match = text.match(/Editor: (http:\/\/\S+)/);
                    if (match) resolve(match[1]);
                });
            });
        }
        await withBrowser(async ({call, evaluate, diagnostics}) => {
            const wait = async expression => {
                for (let n = 0; n < 160; ++n) {
                    if (await evaluate(expression)) return;
                    await new Promise(resolve => setTimeout(resolve, 50));
                }
                throw Error(`${mode}: ${expression}\n${await evaluate('document.querySelector("#errors").textContent')}\n${errors}`);
            };
            const click = async id => {
                await wait(`!document.getElementById('${id}').disabled`);
                const {x, y} = await evaluate(`(() => { const r = document.getElementById('${id}').getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2}; })()`);
                await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1});
                await call("Input.dispatchMouseEvent", {type: "mouseReleased", x, y, button: "left", clickCount: 1});
            };
            const openEffect = async () => evaluate(`document.querySelector('.plugin[data-node="1"]').open = true`);
            const toggleParameters = async () => evaluate(`document.querySelector('.plugin[data-node="1"] button[aria-label="Toggle parameter controls"]').click()`);
            const root = `document.querySelector('.plugin[data-node="1"] .plugin-body > div')?.shadowRoot`;
            const synthRoot = `document.querySelector('.plugin[data-node="0"] .plugin-body > div')?.shadowRoot`;
            const fixture = `${root}?.querySelector(".fixture-ui")`;
            await call("Page.navigate", {url});
            await call("Emulation.setDeviceMetricsOverride", {width: 1100, height: 760, deviceScaleFactor: 1, mobile: false});
            await wait('document.querySelector("#run")?.disabled === false');
            await click("views");
            await click("run");
            await wait(`${synthRoot}?.querySelectorAll(".perone-controls label").length === 3`);
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
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 2, "Expanded plugins stay attached together");
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
            await writeFile(`build/editor-ui-${mode}.png`, Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            await click("stop");
            await wait('document.querySelectorAll(".plugin-body > div").length === 0');
            assert.equal(await evaluate("fixtureFreed"), 2);
            await click("run");
            await wait(`${synthRoot}?.querySelector(".perone-controls")`);
            await openEffect();
            await wait(`${fixture}?.dataset.helper === "7"`);
            await click("views");
            await wait('document.querySelectorAll(".plugin-body > div").length === 0');
            assert.equal(await evaluate("fixtureFreed"), 3);
            await click("views");
            await wait(`${fixture}?.dataset.helper === "7"`);
            // Creation-time gestures and replies survive an asynchronous factory spanning several polls.
            await wait('parseFloat(document.querySelector("#time").textContent) > 2');
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
            await click("views");
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
            await click("stop");
            await wait('document.querySelector("#stop").disabled');
            // An asynchronous factory can complete after Stop; its returned UI must still be freed.
            const freed = await evaluate("fixtureFreed");
            await click("run");
            await wait(`${synthRoot}?.querySelector(".perone-controls")`);
            await evaluate("fixtureWait = true; fixtureResume = undefined");
            await openEffect();
            await wait('typeof fixtureResume === "function"');
            await click("stop");
            await wait('document.querySelector("#stop").disabled');
            await evaluate("fixtureWait = false; fixtureResume()");
            await wait(`fixtureFreed === ${freed + 1}`);
            assert.equal(await evaluate('document.querySelectorAll(".plugin-body > div").length'), 0);
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            assert.deepEqual(diagnostics, []);
            await call("Page.navigate", {url: "about:blank"});
            console.log(`OK: ${mode} DSP, shared custom/generic UI, relative JS/CSS/UI Wasm, automation, edits, messages, selection, stop and restart`);
        });
        if (app && app.exitCode === null) { const exited = once(app, "exit"); app.kill(); await exited; }
        app = undefined;
    }
} finally {
    if (app && app.exitCode === null) { const exited = once(app, "exit"); app.kill(); await exited; }
    if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
    await rm(directory, {recursive: true, force: true});
}
