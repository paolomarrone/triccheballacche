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
const finite = `${directory}/finite.janet`, invalid = `${directory}/invalid.janet`;
const finiteSource = `(def a (daw/plugin "build/test/fixture.perone" {:gain 0.02}))
(def b (daw/plugin "build/test/fixture.perone" {:gain 0.02}))
(daw/output (daw/mix [(daw/track a) (daw/track b)]))
(daw/note a 0 3 60)
(daw/note b 0 3 67)
(daw/end 4.00001)
`;
let app, server;
try {
    await writeFile(entry, source);
    await writeFile(finite, finiteSource);
    await writeFile(invalid, '(error "new score error")');
    const catalog = `${directory}/project.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", directory, "build/test/fixture.perone"]);
    server = serve(0);
    await once(server, "listening");
    for (const mode of ["native", "web"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") {
            app = nativeEditor(entry, {}, {timeout: 120000});
            url = await app.url;
        }
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            await call("Browser.setDownloadBehavior", {behavior: "deny"});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#play")?.disabled === false');
            await click("#views");
            await key(" ", "Space", 2);
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            await wait('document.querySelector(".plugin-body > div")?.shadowRoot?.querySelector("input")');
            await evaluate('window.savedUI = document.querySelector(".plugin-body > div")');
            const revision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
            const time = await evaluate('parseFloat(document.querySelector("#time").value)');
            await set("#code", '(error "Stop must not evaluate the draft")');
            await click("#play");
            await wait('document.querySelector("#state").textContent === "Ready"');
            assert(await evaluate('document.querySelector("#errors").hidden'), "Stop ignores the changed code");
            assert.equal(await evaluate('Number(document.querySelector("#timeline").dataset.revision)'), revision);
            await set("#code", source);
            await evaluate('document.querySelector("#code").focus()');
            await key(" ", "Space", 2);
            await wait('document.querySelector("#play").getAttribute("aria-label") === "Stop"');
            assert.equal(await evaluate('Number(document.querySelector("#timeline").dataset.revision)'), revision, "Resume retains the score");
            await call("Input.dispatchKeyEvent", {type: "keyDown", key: " ", code: "Space", windowsVirtualKeyCode: 32, modifiers: 2, autoRepeat: true});
            await call("Input.dispatchKeyEvent", {type: "keyUp", key: " ", code: "Space", windowsVirtualKeyCode: 32, modifiers: 2});
            assert.equal(await evaluate('document.querySelector("#play").getAttribute("aria-label")'), "Stop", "Holding the shortcut does not toggle repeatedly");
            await set("#code", source.replace('[60 64 67 nil]', '[48 55 60 67]'));
            await click("#save");
            await wait('!document.querySelector("#modified").textContent');
            await click("#apply");
            await wait('document.querySelector("#state").textContent.includes("queued")');
            await wait(`Number(document.querySelector("#timeline").dataset.revision) > ${revision}`);
            assert(await evaluate(`parseFloat(document.querySelector('#time').value) >= ${time}`));
            assert(await evaluate('window.savedUI === document.querySelector(".plugin-body > div")'), "UI instance survives live revision");
            assert(await evaluate('document.querySelector("#errors").hidden'));
            await set("#code", '(gccollect) (repeat 10000 (table 1 2)) (error "live error")');
            await click("#apply");
            await wait('document.querySelector("#errors").textContent.includes("live error")');
            assert.equal(await evaluate('document.querySelector("#play").getAttribute("aria-label")'), "Stop", "Evaluation failure leaves audio running");
            await set("#code", '(gccollect) (while true nil)');
            const preparationStart = performance.now();
            await click("#apply");
            await wait('!document.querySelector("#play").disabled', 70000);
            assert(performance.now() - preparationStart >= 59000, "Preparation gets a full minute before interruption");
            assert(await evaluate('!document.querySelector("#errors").hidden'), "Runaway evaluation is interrupted");
            assert.equal(await evaluate('document.querySelector("#play").getAttribute("aria-label")'), "Stop");
            await set("#code", source);
            await click("#apply");
            await wait('document.querySelector("#state").textContent.includes("queued")');
            await key(" ", "Space", 2);
            await wait('document.querySelector("#state").textContent === "Ready"');
            await set("#code", source.replace('[60 64 67 nil]', '[48 55 60 67]'));
            await click("#play");
            await wait('document.querySelector("#play").getAttribute("aria-label") === "Stop"');
            await click('#play[aria-label="Stop"]');
            await wait('document.querySelector("#state").textContent === "Ready"');
            const stopped = await evaluate('Number(document.querySelector("#time").value)');
            await click("#play");
            await wait(`Number(document.querySelector('#time').value) > ${stopped + .1}`);
            await click('#play[aria-label="Stop"]');
            const seek = async value => {
                await wait('!document.querySelector("#time").disabled');
                await evaluate(`(() => { const t = document.querySelector('#time'); t.focus(); t.value = ${JSON.stringify(String(value))}; })()`);
                await key("Enter");
                await wait('!document.querySelector("#time").disabled');
            };
            await seek(1000000.125);
            await wait('Number(document.querySelector("#time").value) === 1000000.13');
            assert.equal(await evaluate('document.querySelector("#state").textContent'), "Ready");
            assert(await evaluate('window.savedUI === document.querySelector(".plugin-body > div")'), "Seek retains the UI instance");
            await click("#rewind");
            await wait('Number(document.querySelector("#time").value) === 0');
            await wait('Number(document.querySelector("#score-canvas").dataset.from) === 0');
            const ruler = await evaluate(`(() => {
                const c = document.querySelector('#score-canvas'), r = c.getBoundingClientRect();
                return {x: r.x + 1 / Number(c.dataset.scale), y: r.y + 12};
            })()`);
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, ...ruler, button: "left", clickCount: 1});
            await wait('Number(document.querySelector("#time").value) === 1');
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > 1.1');
            await seek(20);
            await wait('Number(document.querySelector("#time").value) > 20');
            await wait('document.querySelector("#marks").childElementCount > 0');

            // Opening another example must replace the playing live session only after successful preparation.
            await evaluate(`(() => {
                const examples = document.querySelector('#examples');
                for (const path of ${JSON.stringify([entry, finite, invalid])}) examples.add(new Option(path, path));
            })()`);
            const open = async path => {
                await set("#examples", path);
                await wait(`document.querySelector('#path').value === ${JSON.stringify(path)} && !document.querySelector('#play').disabled`);
            };
            await open(invalid);
            await click("#apply");
            await wait('document.querySelector("#errors").textContent.includes("new score error")');
            assert.equal(await evaluate('document.querySelector("#play").getAttribute("aria-label")'), "Stop", "A failed new score leaves the old audio running");
            await open(finite);
            assert.equal(await evaluate('document.querySelector("#play").getAttribute("aria-label")'), "Stop", "Browsing another example keeps audio running");
            await click("#apply");
            await wait('!document.querySelector("#play").disabled');
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "", "A new file starts a session instead of attempting a live update");
            await wait('document.querySelectorAll("#track-list .track").length === 2');
            assert.equal(await evaluate('document.querySelector("#code").textContent'), finiteSource);
            assert(!await evaluate('window.savedUI === document.querySelector(".plugin-body > div")'), "A new score creates its own plugin UI");
            await wait('document.querySelector("#play").getAttribute("aria-label") === "Stop"');
            await click('#play[aria-label="Stop"]');
            // The finite session retains endpoint restart and invalid-position rejection.
            await seek(2);
            await wait('Number(document.querySelector("#time").value) === 2');
            await seek(10);
            await wait('!document.querySelector("#errors").hidden');
            assert.equal(await evaluate('Number(document.querySelector("#time").value)'), 2);
            await seek(4.00001);
            await wait('Number(document.querySelector("#time").value) === 4');
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > .1 && Number(document.querySelector("#time").value) < 2');
            await click('#play[aria-label="Stop"]');
            const finiteRevision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
            await set("#path", `${directory}/renamed.janet`);
            await click("#play");
            await wait(`Number(document.querySelector('#timeline').dataset.revision) > ${finiteRevision}`);
            assert(await evaluate('document.querySelector("#errors").hidden'), "Changing the path evaluates even when the text is unchanged");

            // A different file also replaces a live session with a revision still queued.
            await open(entry);
            await click("#apply");
            await wait('!document.querySelector("#play").disabled');
            await key("Enter", "Enter", 2);
            await wait('document.querySelector("#state").textContent.includes("queued")');
            await open(finite);
            await click("#apply");
            await wait('!document.querySelector("#play").disabled');
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            await wait('document.querySelectorAll("#track-list .track").length === 2');
            assert(!await evaluate('document.querySelector("#state").textContent.includes("queued")'));
            await click('#play[aria-label="Stop"]');
            if (mode === "native") {
                await open(entry);
                const slow = source.replace("(daw/tempo 120)", "(daw/tempo 60)");
                await set("#code", slow);
                await click("#play");
                await wait('!document.querySelector("#play").disabled');
                const before = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
                await set("#code", slow.replace('[60 64 67 nil]', '[48 55 60 67]'));
                await click("#apply");
                await wait('document.querySelector("#state").textContent.includes("queued")');
                await evaluate('window.onbeforeunload = null; window.beforeReload = true');
                await call("Page.reload");
                await wait('!window.beforeReload && document.querySelector("#play")?.disabled === false');
                await wait(`Number(document.querySelector('#timeline').dataset.revision) > ${before}`);
                await wait(`Number(document.querySelector('#score-canvas').dataset.revision) > ${before}`);
                assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0,
                    "A reconnected page must not attribute the old draft to the file read from disk");
                assert(await evaluate('document.querySelector("#errors").hidden'));
                await click('#play[aria-label="Stop"]');
            }
            assert.deepEqual(diagnostics, []);
            console.log(`OK: ${mode} Play/Stop, Apply, live-to-finite file switches, pending revisions, seek, UI continuity, errors and timeout`);
        }, {graphics: true, timeout: 120000});
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
