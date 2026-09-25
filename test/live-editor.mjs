import assert from "node:assert/strict";
import {spawn, execFileSync} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const directory = await mkdtemp("build/test/live-editor-"), entry = `${directory}/score.janet`;
const source = `(import ../../../lib/pattern :as p)
(def tone (daw/plugin :tone "build/test/fixture.perone" {:gain 0.04}))
(daw/output (daw/track tone))
(daw/tempo 120)
(daw/score (p/loop (p/map |[:note tone $ 100] (p/steps 0.5 [60 64 67 nil]))))
`;
let app, server;
try {
    await writeFile(entry, source);
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", entry, "build/test/fixture.perone"]);
    server = serve(0);
    await once(server, "listening");
    for (const mode of ["native", "web"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`, stderr = "";
        if (mode === "native") {
            app = spawn("./build/gui", ["--serve", entry]);
            app.stderr.on("data", data => stderr += data);
            url = await new Promise((resolve, reject) => {
                app.on("error", reject);
                app.on("exit", () => reject(Error(stderr)));
                app.stdout.on("data", data => {
                    const match = String(data).match(/Editor: (http:\/\/\S+)/);
                    if (match) resolve(match[1]);
                });
            });
        }
        await withBrowser(async ({call, evaluate, diagnostics}) => {
            const wait = async expression => {
                for (let i = 0; i < 240; ++i) {
                    if (await evaluate(expression)) return;
                    await new Promise(resolve => setTimeout(resolve, 50));
                }
                throw Error(`${mode}: ${expression}\n${await evaluate('document.querySelector("#errors").textContent')}\n${stderr}`);
            };
            const click = async id => {
                await wait(`!document.getElementById('${id}').disabled`);
                const {x,y} = await evaluate(`(() => { const r = document.getElementById('${id}').getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2}; })()`);
                await call("Input.dispatchMouseEvent", {type:"mousePressed",x,y,button:"left",clickCount:1});
                await call("Input.dispatchMouseEvent", {type:"mouseReleased",x,y,button:"left",clickCount:1});
            };
            const edit = code => evaluate(`(() => { const c = document.querySelector('#code'); c.value = ${JSON.stringify(code)}; c.dispatchEvent(new Event('input')); })()`);
            await call("Page.navigate", {url});
            await wait('document.querySelector("#run")?.disabled === false');
            await click("views");
            await click("run");
            await wait('Number(document.querySelector("#notes").dataset.notes) > 0');
            await wait('document.querySelector(".plugin-body > div")?.shadowRoot?.querySelector("input")');
            await evaluate('window.savedUI = document.querySelector(".plugin-body > div")');
            const revision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
            const time = await evaluate('parseFloat(document.querySelector("#time").textContent)');
            await edit(source.replace('[60 64 67 nil]', '[48 55 60 67]'));
            await click("run");
            await wait(`Number(document.querySelector("#timeline").dataset.revision) > ${revision}`);
            assert(await evaluate(`parseFloat(document.querySelector('#time').textContent) >= ${time}`));
            assert(await evaluate('window.savedUI === document.querySelector(".plugin-body > div")'), "UI instance survives live revision");
            assert(await evaluate('document.querySelector("#errors").hidden'));
            await edit('(gccollect) (repeat 10000 (table 1 2)) (error "live error")');
            await click("run");
            await wait('document.querySelector("#errors").textContent.includes("live error")');
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false, "Evaluation failure leaves audio running");
            await edit('(gccollect) (while true nil)');
            await click("run");
            await wait('!document.querySelector("#run").disabled');
            assert(await evaluate('!document.querySelector("#errors").hidden'), "Runaway evaluation is interrupted");
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false);
            await edit(source);
            await click("run");
            await wait('document.querySelector("#state").textContent.includes("queued")');
            await click("stop");
            await wait('document.querySelector("#state").textContent === "Stopped"');
            await click("play");
            await wait('!document.querySelector("#stop").disabled');
            await click("stop");
            assert.deepEqual(diagnostics, []);
            console.log(`OK: ${mode} infinite score, quantized revisions, UI continuity, errors, timeout and restart`);
        });
        if (app) { const exited = once(app, "exit"); app.kill(); await exited; app = undefined; }
    }
} finally {
    app?.kill();
    if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
    await rm(directory, {recursive:true,force:true});
}
