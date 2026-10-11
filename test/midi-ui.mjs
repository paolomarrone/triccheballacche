import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {mkdtemp, writeFile, rm} from "node:fs/promises";
import {once} from "node:events";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

const directory = await mkdtemp("build/test/midi-ui-"), entry = `${directory}/score.janet`, catalog = `${directory}/catalog.json`;
const server = serve(0);
let app;
try {
    await once(server, "listening");
    await writeFile(entry, `(import ../../../lib/pattern :as p)
(def keys (daw/midi :keys))
(def synth (daw/plugin :synth "build/test/fixture.perone" {:gain 0.01}))
(daw/midi-through keys synth)
(daw/midi-output keys)
(daw/output (daw/track synth {:name "Keys"}))
(daw/score (p/loop (p/events 4 [])))`);
    execFileSync("node", ["web/catalog.mjs", catalog, entry, "lib", "build/test/fixture.perone"]);
    for (const mode of ["web", "native"]) {
        const url = mode === "web" ? `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}` :
            await (app = nativeEditor(entry)).url;
        await withBrowser(async ({call, evaluate, wait, click, diagnostics}) => {
            if (mode === "web") await call("Page.addScriptToEvaluateOnNewDocument", {source: `
window.midiSent = [];
window.midiInput = {id: "keys", name: "Test keyboard", type: "input", state: "connected", open: async () => {}, close: async () => {}};
window.midiOutput = {id: "synth", name: "Test output", type: "output", state: "connected", open: async () => {}, close: async () => {},
    clear() {}, send(data, time) { if (window.midiBroken) throw Error("Test MIDI disconnected"); midiSent.push({data: Array.from(data), time}); }};
Object.defineProperty(navigator, "requestMIDIAccess", {value: async () => ({
    inputs: new Map([["keys", midiInput]]), outputs: new Map([["synth", midiOutput]])})});`});
            await call("Page.navigate", {url});
            await wait('!document.querySelector("#play")?.disabled');
            await click("#play");
            await wait('document.querySelector("#state").textContent.startsWith("Playing")');
            await wait('document.querySelector(".routing-cable.midi")');
            await click("#settings");
            await click("#settings-midi-tab");
            await click("#midi-enable");
            if (mode === "web") {
                await wait('document.querySelectorAll(".midi-device").length === 2');
                for (const name of ["In · Test keyboard", "Out · Test output"]) {
                    await evaluate(`(() => { const s = document.querySelector('select[aria-label="${name}"]'); s.value = "0"; s.dispatchEvent(new Event("change")); })()`);
                    await wait(`!document.querySelector('select[aria-label="${name}"]').disabled`);
                }
                await evaluate('midiSent.length = 0; midiInput.onmidimessage({data: new Uint8Array([0x93, 65, 95])})');
                await wait('midiSent.some(p => p.data[0] === 0x93 && p.data[1] === 65 && p.data[2] === 95)');
                assert(await evaluate('midiSent.some(p => Number.isFinite(p.time))'));
            } else await wait('document.querySelector("#midi-devices").children.length || document.querySelector("#midi-empty").textContent');
            await click('#settings-dialog [data-close]');
            await click("#stop");
            await wait('document.querySelector("#state").textContent === "Stopped"');
            if (mode === "web") {
                await evaluate('midiSent.length = 0; midiInput.onmidimessage({data: new Uint8Array([0x93, 70, 90])})');
                await click("#play");
                await wait('midiSent.some(p => p.data[0] === 0x83 && p.data[1] === 65)');
                assert(!await evaluate('midiSent.some(p => p.data[0] === 0x93 && p.data[1] === 70)'), "Stopped input is discarded");
            }
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            if (mode === "web") {
                await evaluate('window.midiBroken = true; midiInput.onmidimessage({data: new Uint8Array([0x93, 72, 90])})');
                await wait('document.querySelector("#state").textContent === "Stopped"');
                await wait('document.querySelector("#errors").textContent.includes("Test MIDI disconnected")');
                await evaluate('window.midiBroken = false');
                await click("#play");
                await wait('document.querySelector("#state").textContent.startsWith("Playing")');
            }
            assert.deepEqual(diagnostics, []);
            console.log(`OK: ${mode} MIDI settings, graph and device lifecycle`);
        });
        if (app) { await app.close(); app = undefined; }
    }
} finally {
    if (app) await app.close();
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    await rm(directory, {recursive: true, force: true});
}
