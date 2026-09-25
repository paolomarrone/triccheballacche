import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

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
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") {
            app = nativeEditor(entry);
            url = await app.url;
        }
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            await call("Page.navigate", {url});
            await wait('document.querySelector("#run")?.disabled === false');
            await click("#views");
            await click("#run");
            await wait('Number(document.querySelector("#notes").dataset.notes) > 0');
            await wait('document.querySelector(".plugin-body > div")?.shadowRoot?.querySelector("input")');
            await evaluate('window.savedUI = document.querySelector(".plugin-body > div")');
            const revision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
            const time = await evaluate('parseFloat(document.querySelector("#time").value)');
            await set("#code", source.replace('[60 64 67 nil]', '[48 55 60 67]'));
            await click("#run");
            await wait(`Number(document.querySelector("#timeline").dataset.revision) > ${revision}`);
            assert(await evaluate(`parseFloat(document.querySelector('#time').value) >= ${time}`));
            assert(await evaluate('window.savedUI === document.querySelector(".plugin-body > div")'), "UI instance survives live revision");
            assert(await evaluate('document.querySelector("#errors").hidden'));
            await set("#code", '(gccollect) (repeat 10000 (table 1 2)) (error "live error")');
            await click("#run");
            await wait('document.querySelector("#errors").textContent.includes("live error")');
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false, "Evaluation failure leaves audio running");
            await set("#code", '(gccollect) (while true nil)');
            await click("#run");
            await wait('!document.querySelector("#run").disabled');
            assert(await evaluate('!document.querySelector("#errors").hidden'), "Runaway evaluation is interrupted");
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false);
            await set("#code", source);
            await click("#run");
            await wait('document.querySelector("#state").textContent.includes("queued")');
            await click("#stop");
            await wait('document.querySelector("#state").textContent === "Stopped"');
            await click("#play");
            await wait('!document.querySelector("#stop").disabled');
            await click("#stop");
            await wait('document.querySelector("#state").textContent === "Stopped"');
            const stopped = await evaluate('Number(document.querySelector("#time").value)');
            await click("#play");
            await wait(`Number(document.querySelector('#time').value) > ${stopped + .1}`);
            await click("#stop");
            const seek = async value => {
                await wait('!document.querySelector("#time").disabled');
                await evaluate(`(() => { const t = document.querySelector('#time'); t.focus(); t.value = ${JSON.stringify(String(value))}; })()`);
                await key("Enter");
                await wait('!document.querySelector("#time").disabled');
            };
            await set("#code", source.replace('[60 64 67 nil]', '[48 55 60 67]')); // Match the active revision, not the cancelled draft.
            await seek(1000000.125);
            await wait('Number(document.querySelector("#time").value) === 1000000.13');
            assert.equal(await evaluate('document.querySelector("#state").textContent'), "Stopped");
            assert(await evaluate('window.savedUI === document.querySelector(".plugin-body > div")'), "Seek retains the UI instance");
            await click("#rewind");
            await wait('Number(document.querySelector("#time").value) === 0');
            await wait('Number(document.querySelector("#notes").dataset.from) === 0');
            const ruler = await evaluate(`(() => {
                const c = document.querySelector('#notes'), r = c.getBoundingClientRect();
                const label = Math.min(230, Math.round(r.width * .3));
                return {x: r.x + label + 1 / Number(c.dataset.scale), y: r.y + 12};
            })()`);
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, ...ruler, button: "left", clickCount: 1});
            await wait('Number(document.querySelector("#time").value) === 1');
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > 1.1');
            await seek(20);
            await wait('Number(document.querySelector("#time").value) > 20');
            await wait('document.querySelector("#marks").childElementCount > 0');
            await click("#stop");

            // A finite score shares the transport, including endpoint restart and invalid-position rejection.
            await set("#code", '(def tone (daw/plugin "build/test/fixture.perone" {:gain 0.04})) (daw/output (daw/track tone)) (daw/note tone 0 3 60) (daw/end 4.00001)');
            await click("#run");
            await wait('!document.querySelector("#stop").disabled');
            await click("#stop");
            await seek(2);
            await wait('Number(document.querySelector("#time").value) === 2');
            await seek(10);
            await wait('!document.querySelector("#errors").hidden');
            assert.equal(await evaluate('Number(document.querySelector("#time").value)'), 2);
            await seek(4.00001);
            await wait('Number(document.querySelector("#time").value) === 4');
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > .1 && Number(document.querySelector("#time").value) < 2');
            await click("#stop");
            assert.deepEqual(diagnostics, []);
            console.log(`OK: ${mode} live revisions, seek, ruler, finite endpoints, Stop/Play, UI continuity, errors and timeout`);
        });
        await app?.close();
        app = undefined;
    }
} finally {
    try { await app?.close(); }
    finally {
        if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
        await rm(directory, {recursive:true,force:true});
    }
}
