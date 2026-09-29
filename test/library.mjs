import assert from "node:assert/strict";
import {once} from "node:events";
import {mkdtemp, readFile, writeFile, rm} from "node:fs/promises";
import {tmpdir} from "node:os";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

const entry = "examples/prog/polpo.janet", original = await readFile(entry, "utf8");
const published = JSON.parse(await readFile("build/web/project.json", "utf8")).map(file => file.path);
const expectedPlugins = published.filter(path => path.endsWith(".perone/product.json"))
    .map(path => path.slice(0, -"/product.json".length)).sort();
const expectedExamples = published.filter(path => path.startsWith("examples/") && path.endsWith(".janet") &&
    !path.startsWith("examples/sources/")).sort();
assert(expectedPlugins.includes("plugins/synth_mono/build/plugin.perone"));
assert(expectedExamples.includes(entry));
assert(expectedExamples.includes("examples/oculus.janet"));
assert(published.includes("examples/sources/oculus-non-vidit.janet"), "Imports must be published with their scores");
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
let native;
try {
    for (const mode of ["web", "native"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html`;
        if (mode === "native") {
            native = nativeEditor(entry);
            url = await native.url;
        }
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            const fileButton = name => `[...document.querySelectorAll("#file-list button")].find(button => button.dataset.name === ${JSON.stringify(name)})`;
            const screenshot = async name => writeFile(`build/test/${name}-${mode}.png`,
                Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            await call("Emulation.setDeviceMetricsOverride", {width: 1200, height: 820, deviceScaleFactor: 1, mobile: false});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#examples")?.disabled === false');
            assert(await evaluate('["open", "save", "settings"].every(id => !document.getElementById(id).textContent.trim())'));
            assert(await evaluate('["open", "save"].every(id => document.getElementById(id).parentElement.id === "file-bar")'));
            assert.equal(await evaluate('document.querySelector("body > nav").lastElementChild.id'), "settings");
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > 0');
            await click("#settings");
            await wait('document.querySelector("#settings-dialog").open');
            const plugins = await evaluate('[...document.querySelectorAll(".catalog-item")].map(row => row.dataset.path).sort()');
            assert.deepEqual(plugins, expectedPlugins, "Settings must list the published plugin bundles");
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
            assert.deepEqual(examples, expectedExamples, "The dropdown must contain scores without support modules");
            const draft = "# draft\n" + original;
            await set("#code", draft);
            await evaluate('globalThis.confirmations = []; globalThis.confirm = message => { confirmations.push(message); return false; }');
            await set("#examples", "examples/gui.janet");
            assert.equal(await evaluate('confirmations.length'), 1);
            assert.equal(await evaluate('document.querySelector("#path").value'), entry);
            assert.equal(await evaluate('document.querySelector("#code").value'), draft);
            assert.equal(await evaluate('document.querySelector("#examples").value'), "");
            await evaluate('globalThis.confirm = () => true');
            await set("#examples", "examples/gui.janet");
            await wait('document.querySelector("#path").value === "examples/gui.janet" && !document.querySelector("#play").disabled');
            const guiSource = await readFile("examples/gui.janet", "utf8");
            assert.equal(await evaluate('document.querySelector("#code").value'), guiSource);
            await click("#open");
            await wait(fileButton("prog"));
            assert.equal(await evaluate('document.querySelector("#code").value'), guiSource, "Opening the picker does not load anything");
            await click('#file-list [data-name="prog"]');
            await wait(fileButton("polpo.janet"));
            await screenshot("files");
            await click('#file-list [data-name="polpo.janet"]');
            await wait('!document.querySelector("#file-dialog").open && !document.querySelector("#play").disabled');
            assert.equal(await evaluate('document.querySelector("#code").value'), original);
            assert.match(await evaluate('document.querySelector("#path").value'), /\/examples\/prog\/polpo\.janet$/);
            await screenshot("toolbar");
            await click("#open");
            await key("Escape", "Escape");
            await wait('!document.querySelector("#file-dialog").open');
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false);
            await set("#path", entry);
            await evaluate('document.querySelector("#path").focus()');
            await key("Enter", "Enter");
            await wait('!document.querySelector("#play").disabled');
            assert.equal(await evaluate('document.querySelector("#path").value'), entry);
            await click("#open");
            await wait('document.querySelector("#file-list").inert === false');
            if (mode === "native") {
                await set("#file-directory", directory);
                await evaluate('document.querySelector("#file-location").requestSubmit()');
                await wait(fileButton("score \"音\".janet"));
                await evaluate(`${fileButton('score "音".janet')}.click()`);
                await wait('!document.querySelector("#file-dialog").open && !document.querySelector("#play").disabled');
                assert.equal(await evaluate('document.querySelector("#code").value'), pickedSource);
                assert.equal(await evaluate('document.querySelector("#path").value'), picked);
            } else {
                const {root} = await call("DOM.getDocument");
                const {nodeId} = await call("DOM.querySelector", {nodeId: root.nodeId, selector: "#file-input"});
                await call("DOM.setFileInputFiles", {nodeId, files: [upload]});
                await wait('!document.querySelector("#file-dialog").open && !document.querySelector("#play").disabled');
                assert.equal(await evaluate('document.querySelector("#code").value'), uploadedSource);
                assert.equal(await evaluate('document.querySelector("#path").value'), "/examples/prog/upload 音.janet");
            }
            await click("#play");
            await wait(`document.querySelector("#errors").textContent.includes(${JSON.stringify(mode === "native" ? "Selected file imports its neighbor" : "Uploaded file imports pattern")})`);
            await set("#examples", entry);
            await wait('document.querySelector("#path").value === "examples/prog/polpo.janet" && !document.querySelector("#play").disabled');
            await click("#open");
            await set("#file-directory", "/no/such/directory");
            await evaluate('document.querySelector("#file-location").requestSubmit()');
            await wait('!document.querySelector("#file-error").hidden');
            await key("Escape", "Escape");
            assert.equal(await evaluate('document.querySelector("#code").value'), original);
            assert.equal(await readFile(entry, "utf8"), original, "Opening and browsing must never save files");
            await click("#stop");
            await wait('!document.querySelector("#play").disabled');
            await set("#examples", "examples/sempiterno.janet");
            await wait('document.querySelector("#path").value === "examples/sempiterno.janet" && !document.querySelector("#play").disabled');
            const revision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
            await click("#play");
            await wait(`Number(document.querySelector("#timeline").dataset.revision) > ${revision}`);
            await wait('parseFloat(document.querySelector("#time").value) > 0');
            assert(await evaluate('document.querySelector("#errors").hidden'));
            assert(await evaluate('Number(document.querySelector("#score-canvas").dataset.notes) > 0'));
            await evaluate('(() => { const t = document.querySelector("#time"); t.focus(); t.value = "60"; })()');
            await key("Enter", "Enter");
            await wait('Number(document.querySelector("#time").value) > 60');
            assert(await evaluate('document.querySelector("#errors").hidden'));
            await click("#stop");
            assert.deepEqual(diagnostics, []);
            await call("Page.navigate", {url: "about:blank"});
            console.log(`OK: ${mode} Settings, catalog, examples, Sempiterno playback, discard guard, file picker, paths/imports, uploads and modal shortcuts`);
        }, {graphics: true});
    }
} finally {
    try { await native?.close(); }
    finally {
        await writeFile("build/test/library-native.log", native?.log || "");
        server.closeAllConnections();
        await new Promise(resolve => server.close(resolve));
        await rm(directory, {recursive: true, force: true});
    }
}
