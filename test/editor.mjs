import assert from "node:assert/strict";
import {spawn} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, readFile, writeFile, stat, chmod, rm} from "node:fs/promises";
import {withBrowser} from "./chromium.mjs";

const directory = await mkdtemp("build/editor-test-");
const path = `${directory}/partitura "音".janet`;
const source = `# Unicode, "quotes", backslash \\ and <html> stay plain text.
(import ./helper)
(import ../../lib/pattern :as p)
(def synth (daw/plugin "build/fixture.perone" {:gain 0.01}))
(daw/track synth)
(defn phrase []
  (def items @[])
  (helper/notes items synth)
  (array/push items [0.25 2.5 [:note synth 64 80]])
  (p/events helper/duration items))
(daw/schedule 0 60 (phrase))
(daw/param synth 0.1 :gain 0.02)
(daw/end helper/duration)
` + "# " + "long line ".repeat(50) + "\n# blank\n".repeat(35);
const line = text => source.split("\n").findIndex(row => row.includes(text)) + 1;
const producer = line("array/push"), caller = line("helper/notes");
await writeFile(`${directory}/helper.janet`, `(def duration 3)
(defn notes [items node]
  (array/push items [0 2 [:note node 60 100]]))
`);
await writeFile(path, source);
await chmod(path, 0o640);
const app = spawn("./build/editor", ["--serve", path]);
let output = "", error = "";
app.stderr.on("data", data => error += data);
const exited = once(app, "exit");
const deadline = setTimeout(() => app.kill("SIGKILL"), 45000);
try {
    const url = await new Promise((resolve, reject) => {
        app.on("error", reject);
        app.on("exit", code => reject(Error(`Editor exited early: ${code}\n${error}`)));
        app.stdout.on("data", data => {
            output += data;
            const match = output.match(/Editor: (http:\/\/\S+)/);
            if (match) resolve(match[1]);
        });
    });
    await withBrowser(async ({call, evaluate, diagnostics}) => {
        const waitFor = async expression => {
            for (let i = 0; i < 150; ++i) {
                if (await evaluate(expression)) return;
                await new Promise(resolve => setTimeout(resolve, 50));
            }
            throw Error(`Timed out: ${expression}\n${await evaluate('document.querySelector("#errors")?.textContent || document.body.textContent')}\n${error}`);
        };
        const set = async (id, value) => evaluate(`(() => {
            const input = document.getElementById(${JSON.stringify(id)});
            input.value = ${JSON.stringify(value)};
            input.dispatchEvent(new Event("input", {bubbles: true}));
        })()`);
        const click = async id => {
            await waitFor(`!document.getElementById(${JSON.stringify(id)}).disabled`);
            await evaluate(`document.getElementById(${JSON.stringify(id)}).click()`);
        };
        await call("Page.navigate", {url});
        await waitFor('document.querySelector("#run")?.disabled === false');
        await call("Emulation.setDeviceMetricsOverride", {width: 900, height: 520, deviceScaleFactor: 1, mobile: false});
        await evaluate(`(() => {
            const call = webui.call.bind(webui);
            window.reports = []; window.ranges = []; window.origins = [];
            window.statusHasTrace = false;
            webui.call = async (...args) => {
                const response = await call(...args), result = JSON.parse(response);
                if (args[1] === "run" && window.openEnd && result.score) result.score.end = null;
                if (args[1] === "run") reports.push(result);
                if (args[1] === "range" && result.lanes) {
                    ranges.push(result);
                    if (window.holdRange) {
                        window.rangeHeld = true;
                        while (window.holdRange) await new Promise(resolve => setTimeout(resolve, 10));
                    }
                }
                if (args[1] === "status" && result.frames) origins.push(...result.frames);
                if (args[1] === "status" && result.trace) statusHasTrace = true;
                return JSON.stringify(result);
            };
        })()`);
        assert.equal(await evaluate('document.querySelector("#code").value'), source);
        assert.equal(await evaluate('document.querySelector("#path").value'), path);
        assert(await evaluate('document.querySelector("#numbers").childElementCount < 30'), "Draw only visible row numbers");
        assert(await evaluate(`(() => {
            const gutter = document.querySelector('#gutter').getBoundingClientRect();
            const number = document.querySelector('#numbers span').getBoundingClientRect();
            return number.left >= gutter.left && number.right <= gutter.right;
        })()`), "Row numbers must remain inside the gutter");
        assert(await evaluate('(document.querySelector("#sheet").clientHeight + document.querySelector("#timeline").clientHeight) > innerHeight * 0.82'), "Keep the editor dense");
        const changed = source.replace("0.01", "0.02");
        await set("code", changed);
        await click("run");
        await waitFor('!document.querySelector("#stop").disabled');
        await waitFor('parseFloat(document.querySelector("#time").textContent) > 0.05');
        await waitFor(`document.querySelector('#marks [data-line="${producer}"]')`);
        assert(await evaluate(`!!document.querySelector('#marks [data-line="${caller}"]')`));
        const report = await evaluate('reports.at(-1).score');
        assert.equal(report.tracks.length, 1);
        assert(await evaluate(`origins.some(frame => frame[0] === ${JSON.stringify(directory + '/helper.janet')} && frame[1] === 3)`));
        await waitFor('Number(document.querySelector("#notes").dataset.notes) === 2');
        const notes = await evaluate('ranges.at(-1).lanes[0].notes');
        assert.deepEqual(notes.map(note => note.slice(1)), [[0, 2, 60, 100], [0.25, 2.5, 64, 80]]);
        await set("code", "# new draft\n" + changed);
        assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0);
        assert((await evaluate('document.querySelector("#state").textContent')).includes("tracking sospeso"));
        await set("code", changed);
        await waitFor(`document.querySelector('#marks [data-line="${producer}"]')`);
        await set("path", `${directory}/another.janet`);
        assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0);
        await set("path", path);
        await waitFor(`document.querySelector('#marks [data-line="${producer}"]')`);
        // Gutter and marks follow the textarea's scroll without changing its text or selection.
        await evaluate(`(() => {
            const code = document.querySelector("#code");
            code.focus(); code.setSelectionRange(4, 8);
            code.scrollTop = 60; code.scrollLeft = 100;
            code.dispatchEvent(new Event("scroll"));
        })()`);
        const geometry = await evaluate(`(() => {
            const code = document.querySelector("#code"), style = getComputedStyle(code);
            return [document.querySelector('#marks [data-line="${producer}"]').getBoundingClientRect().top,
                document.querySelector('#numbers [data-line="${producer}"]').getBoundingClientRect().top,
                code.getBoundingClientRect().top + parseFloat(style.paddingTop) + ${producer - 1} * parseFloat(style.lineHeight) - code.scrollTop];
        })()`);
        assert(Math.max(...geometry) - Math.min(...geometry) < 1, "Highlights must stay aligned when scrolling");
        assert.deepEqual(await evaluate('[document.querySelector("#code").selectionStart, document.querySelector("#code").selectionEnd]'), [4, 8]);
        await evaluate('document.querySelector("#code").scrollTop = document.querySelector("#code").scrollLeft = 0');
        assert.equal(await readFile(path, "utf8"), source, "Run must not save the draft");
        await click("views");
        await click("views");
        await waitFor('document.querySelector("#state").textContent === "In ascolto"');
        const screenshot = await call("Page.captureScreenshot", {format: "png"});
        await writeFile("build/editor.png", Buffer.from(screenshot.data, "base64"));
        await click("stop");
        await waitFor('document.querySelector("#state").textContent === "Fermo"');
        assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0);
        assert.equal(await evaluate('Number(document.querySelector("#notes").dataset.notes)'), 2, "Stop retains the prepared projection");
        const notePoint = await evaluate(`(() => {
            const canvas = document.querySelector('#notes'), rect = canvas.getBoundingClientRect();
            const row = Math.max(58, Math.ceil((canvas.clientHeight - 24) / 7));
            return [rect.left + Math.min(230, Math.round(canvas.clientWidth * 0.3)) + 18,
                rect.top + 24 + row - 8 - 8.5 * (row - 16) / 13];
        })()`);
        const clickNote = async () => {
            await call("Input.dispatchMouseEvent", {type: "mousePressed", x: notePoint[0], y: notePoint[1], button: "left", clickCount: 1});
            await call("Input.dispatchMouseEvent", {type: "mouseReleased", x: notePoint[0], y: notePoint[1], button: "left", clickCount: 1});
        };
        await clickNote();
        await waitFor('document.querySelector("#note-info").title.length > 0');
        assert((await evaluate('document.querySelector("#note-info").textContent')).includes("MIDI 64"));
        assert((await evaluate('document.querySelector("#code").value.slice(document.querySelector("#code").selectionStart, document.querySelector("#code").selectionEnd)')).includes("array/push"));
        await set("code", "# different draft\n" + changed);
        await evaluate('document.querySelector("#code").setSelectionRange(0, 0)');
        await clickNote();
        await new Promise(resolve => setTimeout(resolve, 100));
        assert.equal(await evaluate('document.querySelector("#code").selectionEnd'), 0, "Old origins must not select code in a changed draft");
        await set("code", changed);
        await click("save");
        await waitFor('!document.querySelector("#modified").textContent');
        assert.equal(await readFile(path, "utf8"), changed);
        assert.equal((await stat(path)).mode & 0o777, 0o640);
        await set("path", `${directory}/missing/file.janet`);
        await click("save");
        await waitFor('!document.querySelector("#errors").hidden');
        assert.equal(await readFile(path, "utf8"), changed);
        assert.equal(await evaluate('document.querySelector("#code").value'), changed);
        await set("path", path);
        await set("code", "# draft\nunknown-binding");
        await click("run");
        await waitFor('document.querySelector("#errors").textContent.includes("unknown-binding")');
        assert.equal(await evaluate('reports.at(-1).score'), undefined);
        assert.equal(await evaluate('Number(document.querySelector("#timeline").dataset.revision)'), report.revision);
        assert.equal(await evaluate('Number(document.querySelector("#notes").dataset.notes)'), 2);
        assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0);
        assert((await evaluate('document.querySelector("#errors").textContent')).includes(path));
        assert.equal(await evaluate('document.querySelector("#stop").disabled'), true);
        await set("code", changed);
        await click("run");
        await waitFor('!document.querySelector("#stop").disabled');
        await waitFor('document.querySelector("#state").textContent === "Fermo"');
        assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0);
        assert.equal(await evaluate('statusHasTrace'), false, "Never transfer the complete source report");
        assert.equal(await evaluate('document.querySelector("#errors").hidden'), true);
        await set("path", `${directory}/absent.janet`);
        await click("open");
        await waitFor('!document.querySelector("#errors").hidden');
        assert.equal(await evaluate('document.querySelector("#code").value'), changed);
        await set("path", path);
        await click("open");
        await waitFor('document.querySelector("#errors").hidden && !document.querySelector("#run").disabled');
        const responses = await evaluate(`Promise.all(Array.from({length: 8}, () =>
            webui.call("command", "status", "", "", false).then(JSON.parse)))`);
        assert(responses.every(response => !response.error || response.error.includes("occupato")));
        const dense = `(def lead (daw/plugin "build/fixture.perone" {:gain 0.001}))
(def fx (daw/plugin "build/effect.perone"))
(daw/track lead {:effects [fx]})
(for i 0 9 (daw/track (daw/plugin "build/fixture.perone" {:gain 0.001})))
(daw/master)
(for i 0 1200 (daw/note lead (* i 0.005) 0.03 (+ 60 (% i 12)) 80))
(daw/note lead 0 8 48 90)
(daw/end 12)`;
        await evaluate("window.openEnd = true");
        await set("code", dense);
        await click("run");
        await waitFor('document.querySelector("#notes").dataset.dense === "true"');
        const denseReport = await evaluate('reports.at(-1).score');
        assert.equal(denseReport.end, null, "Exercise navigation and follow with an unknown end");
        assert.equal(denseReport.tracks.length, 11);
        assert.equal(denseReport.tracks[0].length, 3, "Retain the actual effect chain");
        assert.equal(denseReport.tracks.at(-1)[0], -1, "Retain the master lane");
        const denseRange = await evaluate('ranges.at(-1)');
        assert.equal(denseRange.lanes[0].count, 1201);
        assert(denseRange.lanes[0].density.length <= 512);
        assert(denseRange.lanes.length <= 8);
        assert(JSON.stringify(denseRange).length < 30000, "Response cost follows the viewport budget");
        for (let i = 0; i < 3; ++i) await click("zoom-in");
        await waitFor('document.querySelector("#notes").dataset.dense === "false" && Number(document.querySelector("#notes").dataset.notes) > 0');
        await waitFor('Number(document.querySelector("#view-start").value) > 0');
        await click("stop");
        const changeStart = async value => {
            await set("view-start", value);
            await evaluate('document.querySelector("#view-start").dispatchEvent(new Event("change"))');
        };
        const canvasSize = await evaluate('[document.querySelector("#notes").width, document.querySelector("#notes").height]');
        await changeStart(1e9);
        await waitFor('ranges.at(-1).from === 1e9 && document.querySelector("#notes").dataset.notes === "0"');
        assert.deepEqual(await evaluate('[document.querySelector("#notes").width, document.querySelector("#notes").height]'), canvasSize, "Large times must not allocate a song-sized canvas");
        await evaluate('window.holdRange = true; window.rangeHeld = false');
        await changeStart(100);
        await waitFor('window.rangeHeld');
        await changeStart(0);
        await evaluate('window.holdRange = false');
        await waitFor('ranges.at(-1).from === 0 && Number(document.querySelector("#notes").dataset.notes) > 0');
        await evaluate('document.querySelector("#roll").scrollTop = 100000');
        await waitFor('ranges.at(-1).first > 0');
        assert.equal(await evaluate('document.querySelector("#notes").dataset.notes'), "0");
        await evaluate('document.querySelector("#roll").scrollTop = 0');
        await waitFor('ranges.at(-1).first === 0 && Number(document.querySelector("#notes").dataset.notes) > 0');
        const rpc = async args => {
            for (let i = 0; i < 30; ++i) {
                const result = await evaluate(`webui.call("command", ...${JSON.stringify(args)}).then(JSON.parse)`);
                if (!result.error?.includes("occupato")) return result;
                await new Promise(resolve => setTimeout(resolve, 20));
            }
            throw Error("RPC remained busy");
        };
        const stale = await rpc(["range", report.revision, "0", "1", 0, 1, 100]);
        assert(stale.stale && !stale.lanes);
        const invalid = await rpc(["range", denseReport.revision, "0", "1", 0, 1000, 100]);
        assert(invalid.error && !invalid.lanes);
        await evaluate("window.openEnd = false");
        await set("code", changed);
        await click("run");
        await waitFor('!document.querySelector("#stop").disabled');
        assert.deepEqual(diagnostics, []);
        // Closing the frontend while playing must stop and release the native session.
        await call("Page.navigate", {url: "about:blank"});
    });
    const [code, signal] = await exited;
    assert.equal(signal, null, error);
    assert.equal(code, 0, error);
    console.log("OK: dense WebUI editor, bounded timeline/density, follow, stale requests, source selection, native tracking, producers/imports, edits, scroll, Unicode files, unsaved playback, atomic save, diagnostics, recovery and close during audio");
} finally {
    clearTimeout(deadline);
    if (app.exitCode === null && app.signalCode === null) {
        app.kill("SIGTERM");
        await exited;
    }
    await writeFile("build/editor-test.log", output + error);
    await rm(directory, {recursive: true, force: true});
}
