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
        await withBrowser(async ({call, evaluate, wait, click, set, diagnostics}) => {
            if (mode === "web") await call("Page.addScriptToEvaluateOnNewDocument", {source: `
window.midiSent = [];
window.midiInput = {id: "keys", name: "Test keyboard", type: "input", state: "connected", open: async () => {}, close: async () => {}};
window.midiOutput = {id: "synth", name: "Test output", type: "output", state: "connected", open: async () => {}, close: async () => {},
    clear() {}, send(data, time) { if (window.midiBroken) throw Error("Test MIDI disconnected"); midiSent.push({data: Array.from(data), time}); }};
Object.defineProperty(navigator, "requestMIDIAccess", {value: async () => ({
    inputs: new Map([["keys", midiInput]]), outputs: new Map([["synth", midiOutput]])})});`});
            await call("Page.navigate", {url});
            await wait('!document.querySelector("#play")?.disabled');
            await evaluate(`import(${JSON.stringify(mode === "web" ? "../web/editor.js" : "./native.js")}).then(backend => { window.midiBackend = backend; })`);
            await click("#apply");
            await wait('document.querySelector("#timeline").dataset.revision === "1"');
            assert.equal(await evaluate('document.querySelector("#state").textContent'), "Ready");
            assert.equal(await evaluate('Number(document.querySelector("#time").value)'), 0);
            assert(await evaluate('document.querySelector("#settings-audio-engine").checked'));
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
            assert.equal(await evaluate('Number(document.querySelector("#time").value)'), 0);
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > 0');
            const enabled = await evaluate('midiBackend.command("audio", 1)');
            assert(enabled.audio && enabled.playing && !enabled.error, "Enabling audio twice must preserve playback");
            await click('#play[aria-label="Stop"]');
            await wait('document.querySelector("#state").textContent === "Ready"');
            if (mode === "web") {
                await evaluate('midiSent.length = 0; midiInput.onmidimessage({data: new Uint8Array([0x93, 70, 90])})');
                await wait('midiSent.some(p => p.data[0] === 0x93 && p.data[1] === 70)');
                assert(!await evaluate('midiSent.some(p => p.data[0] === 0x83 && p.data[1] === 65)'), "Transport stop preserves keyboard notes");
                await evaluate('midiInput.onmidimessage({data: new Uint8Array([0x83, 65, 0])})');
                await wait('midiSent.some(p => p.data[0] === 0x83 && p.data[1] === 65)');
            }
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            const position = await evaluate('document.querySelector("#time").value');
            await click("#settings");
            await click("#settings-audio-tab");
            await click("#settings-audio-engine");
            await wait('!document.querySelector("#settings-audio-engine").checked && !document.querySelector("#settings-audio-engine").disabled');
            const stopped = await evaluate('midiBackend.command("stop")');
            assert(!stopped.audio && !stopped.playing && !stopped.error, "Stop must not enable a disabled engine");
            if (mode === "web") {
                await evaluate('midiSent.length = 0; midiInput.onmidimessage({data: new Uint8Array([0x93, 73, 90])})');
                await new Promise(resolve => setTimeout(resolve, 100));
                assert(!await evaluate('midiSent.some(p => p.data[0] === 0x93 && p.data[1] === 73)'));
            }
            await click("#settings-audio-engine");
            await wait('document.querySelector("#settings-audio-engine").checked && !document.querySelector("#settings-audio-engine").disabled');
            assert.equal(await evaluate('document.querySelector("#time").value'), position);
            await click('#settings-dialog [data-close]');
            if (mode === "web") {
                await evaluate('window.midiBroken = true; midiInput.onmidimessage({data: new Uint8Array([0x93, 72, 90])})');
                await wait('document.querySelector("#state").textContent === "Audio off"');
                await wait('document.querySelector("#errors").textContent.includes("Test MIDI disconnected")');
                await evaluate('window.midiBroken = false');
                await click("#play");
                await wait('document.querySelector("#state").textContent.startsWith("Playing")');
            }
            // A bare graph prepares and exposes plugins without a fake duration, on both backends.
            const graph = '(def keys (daw/midi :keys)) (def synth (daw/plugin :synth "build/test/fixture.perone" {:gain 0.01})) (daw/midi-through keys synth) (daw/midi-output keys) (daw/output (daw/track synth))';
            if (await evaluate('document.querySelector("#play").getAttribute("aria-label") === "Stop"')) await click('#play[aria-label="Stop"]');
            await set("#code", graph);
            await click("#apply");
            await wait('!document.querySelector("#apply").disabled && document.querySelector("#settings-audio-engine").checked');
            assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
            assert.equal(await evaluate('Number(document.querySelector("#time").value)'), 0);
            assert.equal(await evaluate('document.querySelector("#state").textContent'), "Ready");
            await click("#views");
            await wait('document.querySelector("#plugin-list .plugin")');
            await click("#settings");
            await click("#settings-midi-tab");
            if (mode === "web") {
                await wait('!document.querySelector(".midi-device select").disabled');
                await set('.midi-device select', "0");
                await wait('!document.querySelector(".midi-device select").disabled');
            }
            await click("#settings-audio-tab");
            await click("#settings-audio-engine");
            await wait('!document.querySelector("#audio-rate").disabled');
            await set("#audio-rate", "44100");
            await wait('!document.querySelector("#audio-rate").disabled');
            await click("#settings-midi-tab");
            if (mode === "web") {
                await wait('[...document.querySelectorAll(".midi-device select")].every(s => s.disabled && s.value === "-1" && s.options.length === 1)');
                assert((await evaluate('midiBackend.command("midi-devices")')).devices.every(port => port.node === -1));
            }
            await click('#settings-dialog [data-close]');
            await click("#apply");
            await wait('!document.querySelector("#apply").disabled && document.querySelector("#settings-audio-engine").checked');
            await click("#settings");
            await click("#settings-audio-tab");
            await wait('document.querySelector("#audio-rate").value === "44100" && !document.querySelector("#settings-audio-engine").disabled');
            assert(await evaluate('document.querySelector("#settings-error").hidden'));
            await click('#settings-dialog [data-close]');
            assert.deepEqual(diagnostics, []);
            // Avoid the draft confirmation on test teardown.
            await evaluate('window.onbeforeunload = null');
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
