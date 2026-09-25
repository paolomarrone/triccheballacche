import assert from "node:assert/strict";
import {spawn} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, readFile, writeFile, rm} from "node:fs/promises";
import {tmpdir} from "node:os";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const entry = "examples/prog/polpo.janet", original = await readFile(entry, "utf8");
const directory = await mkdtemp(tmpdir() + "/triccheballacche-files-");
const picked = `${directory}/score "音".janet`;
const pickedSource = '(import ./helper)\n(error helper/message)\n';
await writeFile(picked, pickedSource);
await writeFile(`${directory}/helper.janet`, '(def message "Selected file imports its neighbor")\n');
const upload = `${directory}/upload 音.janet`;
const uploadedSource = '(import ../../lib/pattern :as p)\n(error "Uploaded file imports pattern")\n';
await writeFile(upload, uploadedSource);
const server = serve(0);
await once(server, "listening");
let native, exited, output = "", inventory;
try {
    for (const mode of ["web", "native"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html`;
        if (mode === "native") {
            native = spawn("./build/gui", ["--serve", entry]);
            native.stderr.on("data", bytes => output += bytes);
            exited = once(native, "exit");
            url = await new Promise((resolve, reject) => {
                let text = "";
                native.on("error", reject);
                native.on("exit", () => reject(Error(output)));
                native.stdout.on("data", bytes => {
                    text += bytes;
                    const match = text.match(/Editor: (http:\/\/\S+)/);
                    if (match) resolve(match[1]);
                });
            });
        }
        await withBrowser(async ({call, evaluate, diagnostics}) => {
            const wait = async expression => {
                for (let i = 0; i < 200; ++i) {
                    if (await evaluate(expression)) return;
                    await new Promise(resolve => setTimeout(resolve, 50));
                }
                throw Error(`${mode}: ${expression}\n${await evaluate('document.querySelector("#errors").textContent')}\n${output}`);
            };
            const click = async selector => {
                await wait(`document.querySelector(${JSON.stringify(selector)})?.disabled === false`);
                const {x, y} = await evaluate(`(() => {
                    const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();
                    return {x:r.x+r.width/2,y:r.y+r.height/2};
                })()`);
                await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1});
                await call("Input.dispatchMouseEvent", {type: "mouseReleased", x, y, button: "left", clickCount: 1});
            };
            const set = (id, value) => evaluate(`(() => {
                const input = document.getElementById(${JSON.stringify(id)}); input.value = ${JSON.stringify(value)};
                input.dispatchEvent(new Event('input')); input.dispatchEvent(new Event('change'));
            })()`);
            const key = async (key, code) => {
                const windowsVirtualKeyCode = key === "Escape" ? 27 : 13;
                await call("Input.dispatchKeyEvent", {type: "keyDown", key, code, windowsVirtualKeyCode});
                await call("Input.dispatchKeyEvent", {type: "keyUp", key, code, windowsVirtualKeyCode});
            };
            const fileButton = name => `[...document.querySelectorAll("#file-list button")].find(button => button.dataset.name === ${JSON.stringify(name)})`;
            const screenshot = async name => writeFile(`build/test/${name}-${mode}.png`,
                Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            await call("Emulation.setDeviceMetricsOverride", {width: 1200, height: 820, deviceScaleFactor: 1, mobile: false});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#examples")?.disabled === false');
            assert(await evaluate('["open", "save", "settings"].every(id => !document.getElementById(id).textContent.trim())'));
            assert(await evaluate('["open", "save"].every(id => document.getElementById(id).parentElement.id === "file-bar")'));
            assert.equal(await evaluate('document.querySelector("body > nav").lastElementChild.id'), "settings");
            await click("#run");
            await wait('Number(document.querySelector("#time").textContent.split(" ")[0]) > 0');
            await click("#settings");
            await wait('document.querySelector("#settings-dialog").open');
            const plugins = await evaluate('[...document.querySelectorAll(".catalog-item")].map(row => row.dataset.path).sort()');
            assert.equal(plugins.length, 48);
            assert(plugins.every(path => !/\/(fxpp_|synthpp_)/.test(path)));
            assert(await evaluate('document.querySelector("#plugin-paths").children.length > 0'));
            assert.match(await evaluate('document.querySelector("#catalog-list").textContent'), /in use/);
            await click("#settings-plugins");
            assert.equal(await evaluate('document.activeElement.id'), "settings-catalog");
            await screenshot("settings");
            await key("Escape", "Escape");
            await wait('!document.querySelector("#settings-dialog").open');
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false, "Escape closes Settings without stopping playback");
            assert.equal(await evaluate('document.activeElement.id'), "settings");
            const examples = await evaluate('[...document.querySelector("#examples").options].map(option => option.value).filter(Boolean).sort()');
            assert.equal(examples.length, 12);
            assert(examples.includes(entry));
            if (inventory) assert.deepEqual({plugins, examples}, inventory, "Both backends expose the same library");
            inventory = {plugins, examples};
            const draft = "# draft\n" + original;
            await set("code", draft);
            await evaluate('globalThis.confirmations = []; globalThis.confirm = message => { confirmations.push(message); return false; }');
            await set("examples", "examples/gui.janet");
            assert.equal(await evaluate('confirmations.length'), 1);
            assert.equal(await evaluate('document.querySelector("#path").value'), entry);
            assert.equal(await evaluate('document.querySelector("#code").value'), draft);
            assert.equal(await evaluate('document.querySelector("#examples").value'), "");
            await evaluate('globalThis.confirm = () => true');
            await set("examples", "examples/gui.janet");
            await wait('document.querySelector("#path").value === "examples/gui.janet" && !document.querySelector("#run").disabled');
            const guiSource = await readFile("examples/gui.janet", "utf8");
            assert.equal(await evaluate('document.querySelector("#code").value'), guiSource);
            await click("#open");
            await wait(fileButton("prog"));
            assert.equal(await evaluate('document.querySelector("#code").value'), guiSource, "Opening the picker does not load anything");
            await click('#file-list [data-name="prog"]');
            await wait(fileButton("polpo.janet"));
            await screenshot("files");
            await click('#file-list [data-name="polpo.janet"]');
            await wait('!document.querySelector("#file-dialog").open && !document.querySelector("#run").disabled');
            assert.equal(await evaluate('document.querySelector("#code").value'), original);
            assert.match(await evaluate('document.querySelector("#path").value'), /\/examples\/prog\/polpo\.janet$/);
            await screenshot("toolbar");
            await click("#open");
            await key("Escape", "Escape");
            await wait('!document.querySelector("#file-dialog").open');
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false);
            await set("path", entry);
            await evaluate('document.querySelector("#path").focus()');
            await key("Enter", "Enter");
            await wait('!document.querySelector("#run").disabled');
            assert.equal(await evaluate('document.querySelector("#path").value'), entry);
            await click("#open");
            await wait('document.querySelector("#file-list").inert === false');
            if (mode === "native") {
                await set("file-directory", directory);
                await evaluate('document.querySelector("#file-location").requestSubmit()');
                await wait(fileButton("score \"音\".janet"));
                await evaluate(`${fileButton('score "音".janet')}.click()`);
                await wait('!document.querySelector("#file-dialog").open && !document.querySelector("#run").disabled');
                assert.equal(await evaluate('document.querySelector("#code").value'), pickedSource);
                assert.equal(await evaluate('document.querySelector("#path").value'), picked);
            } else {
                const {root} = await call("DOM.getDocument");
                const {nodeId} = await call("DOM.querySelector", {nodeId: root.nodeId, selector: "#file-input"});
                await call("DOM.setFileInputFiles", {nodeId, files: [upload]});
                await wait('!document.querySelector("#file-dialog").open && !document.querySelector("#run").disabled');
                assert.equal(await evaluate('document.querySelector("#code").value'), uploadedSource);
                assert.equal(await evaluate('document.querySelector("#path").value'), "/examples/prog/upload 音.janet");
            }
            await click("#run");
            await wait(`document.querySelector("#errors").textContent.includes(${JSON.stringify(mode === "native" ? "Selected file imports its neighbor" : "Uploaded file imports pattern")})`);
            await set("examples", entry);
            await wait('document.querySelector("#path").value === "examples/prog/polpo.janet" && !document.querySelector("#run").disabled');
            await click("#open");
            await set("file-directory", "/no/such/directory");
            await evaluate('document.querySelector("#file-location").requestSubmit()');
            await wait('!document.querySelector("#file-error").hidden');
            await key("Escape", "Escape");
            assert.equal(await evaluate('document.querySelector("#code").value'), original);
            assert.equal(await readFile(entry, "utf8"), original, "Opening and browsing must never save files");
            assert.deepEqual(diagnostics, []);
            await call("Page.navigate", {url: "about:blank"});
            console.log(`OK: ${mode} Settings, catalog, examples, discard guard, file picker, paths/imports, uploads and modal shortcuts`);
        });
    }
} finally {
    if (native && native.exitCode === null && native.signalCode === null) { native.kill("SIGTERM"); await exited; }
    await writeFile("build/test/library-native.log", output);
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    await rm(directory, {recursive: true, force: true});
}
