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
            window.reports = [];
            window.statusHasTrace = false;
            webui.call = async (...args) => {
                const response = await call(...args), result = JSON.parse(response);
                if (args[1] === "run") reports.push(result);
                if (args[1] === "status" && result.trace) statusHasTrace = true;
                return response;
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
        assert(await evaluate('document.querySelector("#sheet").clientHeight > innerHeight * 0.85'), "Keep the editor dense");
        const changed = source.replace("0.01", "0.02");
        await set("code", changed);
        await click("run");
        await waitFor('!document.querySelector("#stop").disabled');
        await waitFor('parseFloat(document.querySelector("#time").textContent) > 0.05');
        await waitFor(`document.querySelector('#marks [data-line="${producer}"]')`);
        assert(await evaluate(`!!document.querySelector('#marks [data-line="${caller}"]')`));
        const report = await evaluate('reports.at(-1).trace');
        assert.equal(report.events.length, 3);
        assert(report.locations.flat().some(frame => frame.file === `${directory}/helper.janet` && frame.line === 3));
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
        assert.equal(await evaluate('reports.at(-1).trace'), null);
        assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0);
        assert((await evaluate('document.querySelector("#errors").textContent')).includes(path));
        assert.equal(await evaluate('document.querySelector("#stop").disabled'), true);
        await set("code", changed);
        await click("run");
        await waitFor('!document.querySelector("#stop").disabled');
        await waitFor('document.querySelector("#state").textContent === "Fermo"');
        assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0);
        assert.equal(await evaluate('statusHasTrace'), false, "Transfer the report only once per run");
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
        await click("run");
        await waitFor('!document.querySelector("#stop").disabled');
        assert.deepEqual(diagnostics, []);
        // Closing the frontend while playing must stop and release the native session.
        await call("Page.navigate", {url: "about:blank"});
    });
    const [code, signal] = await exited;
    assert.equal(signal, null, error);
    assert.equal(code, 0, error);
    console.log("OK: dense WebUI editor, native source tracking, producers/imports, edits, scroll, Unicode files, unsaved playback, atomic save, diagnostics, recovery and close during audio");
} finally {
    clearTimeout(deadline);
    if (app.exitCode === null && app.signalCode === null) {
        app.kill("SIGTERM");
        await exited;
    }
    await writeFile("build/editor-test.log", output + error);
    await rm(directory, {recursive: true, force: true});
}
