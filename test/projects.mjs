import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {once} from "node:events";
import {mkdtemp, readFile, writeFile, rm, access} from "node:fs/promises";
import {Project, openProject, resolve, relative} from "../editor/project.js";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

assert.equal(resolve("./lib/../lib/beat.janet", "/songs/demo"), "/songs/demo/lib/beat.janet");
assert.equal(relative("/songs/lib/beat.janet", "/songs/demo"), "../lib/beat.janet");
assert.equal(relative("/main.janet", "/"), "main.janet");
const project = await openProject("songs/main.janet", "entry", "/", () => assert.fail());
assert(!project.dirty());
const snapshot = project.snapshot();
project.add("songs/lib/notes.janet", "module", false);
assert(project.dirty() && !project.matches(snapshot));
assert.equal(project.entry, "/songs/main.janet");
assert.equal(snapshot.sources["/songs/lib/notes.janet"], undefined);
const imported = await openProject("songs/project.tb.json", project.description(), "/", async path => project.documents.get(path).text);
assert(!imported.dirty());
assert.equal(imported.active, imported.entry);
assert.equal(imported.current.text, "entry");
await assert.rejects(openProject("songs/project.tb.json", '{"entry":"absent.janet","files":["main.janet"]}', "/", async () => ""), /entry is missing/);
await assert.rejects(openProject("songs/project.tb.json", '{"entry":"main.janet","files":["main.janet","./main.janet"]}', "/", async () => ""), /Duplicate/);
assert.throws(() => new Project("/", "/songs").add("bad.janet", "\0"), /NUL/);

const directory = await mkdtemp("build/test/projects-");
const source = `(import ./lib/phrase)
(import ../../../lib/pattern :as p)
(def tone (daw/plugin :tone "build/test/fixture.perone" {:gain 0.01}))
(daw/output (daw/track tone))
(daw/tempo 120)
(daw/score (p/loop (phrase/notes tone)))
`;
const helper = `(import ../../../../lib/pattern :as p)
(defn notes [node]
  (p/map |[:note node $ 90] (p/steps 0.5 [60 64 67 72])))
`;
let app, server;
try {
    const entry = `${directory}/main.janet`;
    await writeFile(entry, source);
    const catalog = `${directory}/catalog.json`;
    execFileSync("node", ["web/catalog.mjs", catalog, "lib", directory, "build/test/fixture.perone"]);
    server = serve(0);
    await once(server, "listening");
    for (const mode of ["web", "native"]) {
        let url = `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        if (mode === "native") { app = nativeEditor(entry); url = await app.url; }
        await withBrowser(async ({call, evaluate, wait, click, set, key, diagnostics}) => {
            await call("Browser.setDownloadBehavior", {behavior: "deny"});
            await call("Page.navigate", {url});
            await wait('document.querySelector("#play")?.disabled === false');
            await click("#new-document");
            await set("#document-name", "lib/phrase.janet");
            await evaluate('document.querySelector("#document-form").requestSubmit()');
            await wait('!document.querySelector("#document-dialog").open');
            await set("#code", helper);
            await evaluate('window.helperEditor = document.querySelector("#code"); helperEditor.setSelectionRange(4, 9)');
            await click('#document-tabs button:first-child');
            assert.equal(await evaluate('document.querySelector("#code").value'), source);
            await click('#document-tabs button:last-child');
            assert.deepEqual(await evaluate('[document.querySelector("#code") === helperEditor, helperEditor.selectionStart, helperEditor.selectionEnd]'), [true, 4, 9]);
            await evaluate(`(() => {
                helperEditor.focus(); helperEditor.setSelectionRange(helperEditor.value.length, helperEditor.value.length);
                document.execCommand("insertText", false, "# temporary edit\\n");
            })()`);
            assert(await evaluate('helperEditor.value.endsWith("# temporary edit\\n")'));
            await click('#document-tabs button:first-child');
            await click('#document-tabs button:last-child');
            await evaluate('helperEditor.focus(); document.execCommand("undo")');
            assert.equal(await evaluate('helperEditor.value'), helper, "Undo history survives tab switches");
            await click("#play");
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            assert(await evaluate('document.querySelector("#errors").hidden'), "Play evaluates the entry, using an unsaved library");
            await wait('document.querySelector("#marks").childElementCount > 0');
            await assert.rejects(access(`${directory}/lib/phrase.janet`), "Playing drafts must not write files");
            const revision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
            await click('#document-tabs button:first-child');
            await click("#play");
            await wait('!document.querySelector("#play").disabled');
            assert.equal(await evaluate('Number(document.querySelector("#timeline").dataset.revision)'), revision, "Switching documents does not reevaluate");
            await click('#document-tabs button:last-child');
            await set("#code", helper.replace("60 64", "48 55"));
            await click("#play");
            await wait(`Number(document.querySelector("#timeline").dataset.revision) > ${revision}`);
            assert(await evaluate('document.querySelector("#errors").hidden'), "A library edit participates in live updates");
            await set("#code", '(error "unsaved module error")');
            await click("#play");
            await wait('document.querySelector("#errors").textContent.includes("unsaved module error")');
            assert.equal(await evaluate('document.querySelector("#stop").disabled'), false, "An invalid module keeps the previous revision playing");
            await set("#code", helper);
            await click("#stop");
            await click("#project");
            await click("#save-project");
            await wait('!document.title.startsWith("* ") && !document.querySelector("#play").disabled');
            if (mode === "native") {
                assert.equal(await readFile(`${directory}/lib/phrase.janet`, "utf8"), helper);
                assert.deepEqual(JSON.parse(await readFile(`${directory}/project.tb.json`, "utf8")), {entry: "main.janet", files: ["main.janet", "lib/phrase.janet"]});
            } else {
                await evaluate('window.beforeProjectReload = true');
                await call("Page.reload");
                await wait('!window.beforeProjectReload && document.querySelector("#play")?.disabled === false');
            }
            await set("#path", `${directory}/project.tb.json`);
            await evaluate('document.querySelector("#path").focus()');
            await key("Enter");
            await wait('document.querySelectorAll("#document-tabs button").length === 2 && !document.querySelector("#play").disabled');
            assert.equal(await evaluate('document.querySelector("#code").value'), source);
            await click('#document-tabs button:last-child');
            assert.equal(await evaluate('document.querySelector("#code").value'), helper, "Saved modules survive reopening and browser reload");
            await click("#play");
            await wait('Number(document.querySelector("#score-canvas").dataset.notes) > 0');
            assert(await evaluate('document.querySelector("#errors").hidden'));
            await click("#stop");
            await click("#new-document");
            await set("#document-name", "lib/phrase.janet");
            await evaluate('document.querySelector("#document-form").requestSubmit()');
            await wait('document.querySelector("#document-error").textContent.includes("already")');
            await key("Escape");
            assert.equal(await evaluate('document.querySelector("#code").value'), helper, "Duplicate documents preserve the existing buffer");

            await click("#project");
            const screenshot = await call("Page.captureScreenshot", {format: "png"});
            await writeFile(`build/test/projects-${mode}.png`, Buffer.from(screenshot.data, "base64"));
            await click("#new-project");
            await set("#document-name", `${directory}/new-${mode}`);
            await evaluate('document.querySelector("#document-form").requestSubmit()');
            await wait('!document.querySelector("#document-dialog").open && !document.querySelector("#play").disabled');
            assert.equal(await evaluate('document.querySelectorAll("#document-tabs button").length'), 1);
            const standalone = '(def tone (daw/plugin "build/test/fixture.perone")) (daw/output (daw/track tone)) (daw/end 1)';
            await set("#code", standalone);
            await click("#project");
            await click("#save-project");
            await wait('!document.title.startsWith("* ") && !document.querySelector("#play").disabled');
            await click("#play");
            await wait('document.querySelectorAll("#track-list .track").length === 1 && !document.querySelector("#play").disabled');
            assert(await evaluate('document.querySelector("#errors").hidden'));
            await click("#stop");
            assert.deepEqual(diagnostics, []);
        }, {graphics: true});
        if (app) { await app.close(); app = undefined; }
        console.log(`OK: ${mode} project documents, unsaved imports, entry point, module tracking, live updates, errors and persistence`);
    }
} finally {
    await app?.close();
    if (server) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
    await rm(directory, {recursive: true, force: true});
}
