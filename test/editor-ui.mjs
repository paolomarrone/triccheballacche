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
            await writeFile(`${bundle}/ui/helper.wasm`, new Uint8Array([
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
            const select = async (id, value) => evaluate(`(() => {
                const element = document.getElementById('${id}'); element.value = '${value}'; element.dispatchEvent(new Event('change'));
            })()`);
            const root = 'document.querySelector("#plugin-ui > div")?.shadowRoot';
            const fixture = `${root}?.querySelector(".fixture-ui")`;
            await call("Page.navigate", {url});
            await call("Emulation.setDeviceMetricsOverride", {width: 1100, height: 760, deviceScaleFactor: 1, mobile: false});
            await wait('document.querySelector("#run")?.disabled === false');
            await click("views");
            await click("run");
            await wait(`${root}?.querySelectorAll(".perone-controls label").length === 3`);
            await select("plugin-node", 1);
            await wait(`${fixture}?.dataset.helper === "7"`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .2) < .0001`);
            assert.equal(await evaluate(`getComputedStyle(${fixture}).display`), "grid");
            await evaluate(`${root}.querySelector('#set-gain').click()`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .3) < .0001`);
            await wait(`Math.abs(Number(${fixture}.dataset.meter) - .3) < .0001`);
            await evaluate(`${root}.querySelector('#send-message').click()`);
            await wait(`${root}.querySelector('output').textContent === "0,127,255"`);
            await select("plugin-kind", "generic");
            await wait(`${root}?.querySelectorAll(".perone-controls label").length === 3`);
            assert.equal(await evaluate("fixtureFreed"), 1);
            await select("plugin-kind", "auto");
            await wait(`${fixture}?.dataset.helper === "7"`);
            await wait(`Math.abs(Number(${fixture}.dataset.gain) - .3) < .0001`);
            await evaluate("fixtureCallbacks[0].set_parameter(1, .9); fixtureCallbacks[0].msg_write(new Uint8Array([42]))");
            await new Promise(resolve => setTimeout(resolve, 150));
            assert(Math.abs(await evaluate(`Number(${fixture}.dataset.gain)`) - .3) < .0001, "Freed callbacks must not edit the DSP");
            await writeFile(`build/editor-ui-${mode}.png`, Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            await click("stop");
            await wait('document.querySelector("#plugin-ui").childElementCount === 0');
            assert.equal(await evaluate("fixtureFreed"), 2);
            await click("run");
            await wait(`${root}?.querySelector(".perone-controls")`);
            await select("plugin-node", 1);
            await wait(`${fixture}?.dataset.helper === "7"`);
            await click("views");
            await wait('document.querySelector("#plugin-ui").childElementCount === 0');
            assert.equal(await evaluate("fixtureFreed"), 3);
            await click("views");
            await wait(`${fixture}?.dataset.helper === "7"`);
            await click("stop");
            await wait('document.querySelector("#stop").disabled');
            // An asynchronous factory can complete after Stop; its returned UI must still be freed.
            const freed = await evaluate("fixtureFreed");
            await click("run");
            await wait(`${root}?.querySelector(".perone-controls")`);
            await evaluate("fixtureWait = true");
            await select("plugin-node", 1);
            await wait('typeof fixtureResume === "function"');
            await click("stop");
            await wait('document.querySelector("#stop").disabled');
            await evaluate("fixtureWait = false; fixtureResume()");
            await wait(`fixtureFreed === ${freed + 1}`);
            assert.equal(await evaluate('document.querySelector("#plugin-ui").childElementCount'), 0);
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
