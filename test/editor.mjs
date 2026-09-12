import assert from "node:assert/strict";
import {spawn} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, readFile, writeFile, stat, chmod, rm} from "node:fs/promises";
import {withBrowser} from "./chromium.mjs";

const directory = await mkdtemp("build/editor-test-");
const path = `${directory}/partitura "音".janet`;
const source = `# Unicode, "quotes", backslash \\ and <html> stay plain text.
(import ./helper)
(def p (daw/plugin "build/fixture.perone" {:gain 0.01}))
(daw/track p)
(daw/end helper/duration)
`;
await writeFile(`${directory}/helper.janet`, "(def duration 2)\n");
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
        assert.equal(await evaluate('document.querySelector("#code").value'), source);
        assert.equal(await evaluate('document.querySelector("#path").value'), path);
        const changed = source.replace("0.01", "0.02");
        await set("code", changed);
        await click("run");
        await waitFor('!document.querySelector("#stop").disabled');
        await waitFor('parseFloat(document.querySelector("#time").textContent) > 0.05');
        assert.equal(await readFile(path, "utf8"), source, "Run must not save the draft");
        await click("views");
        await click("views");
        await waitFor('document.querySelector("#state").textContent === "In ascolto"');
        const screenshot = await call("Page.captureScreenshot", {format: "png"});
        await writeFile("build/editor.png", Buffer.from(screenshot.data, "base64"));
        await click("stop");
        await waitFor('document.querySelector("#state").textContent === "Fermo"');
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
        assert((await evaluate('document.querySelector("#errors").textContent')).includes(path));
        assert.equal(await evaluate('document.querySelector("#stop").disabled'), true);
        await set("code", changed);
        await click("run");
        await waitFor('!document.querySelector("#stop").disabled');
        await waitFor('document.querySelector("#state").textContent === "Fermo"');
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
    console.log("OK: WebUI editor, Unicode files, unsaved playback, atomic save, diagnostics, recovery, serialized calls and close during audio");
} finally {
    clearTimeout(deadline);
    if (app.exitCode === null && app.signalCode === null) {
        app.kill("SIGTERM");
        await exited;
    }
    await writeFile("build/editor-test.log", output + error);
    await rm(directory, {recursive: true, force: true});
}
