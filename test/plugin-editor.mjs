import assert from "node:assert/strict";
import {mkdtemp, readFile, writeFile, rm} from "node:fs/promises";
import {withBrowser} from "./chromium.mjs";
import {nativeEditor} from "./native.mjs";

const format = process.argv[2];
assert(["clap", "lv2"].includes(format), "Usage: plugin-editor.mjs clap|lv2");
const variable = `${format.toUpperCase()}_PATH`, title = format.toUpperCase();
const directory = await mkdtemp(`build/test/${format}-editor-`);
const entry = `${directory}/score.janet`, previous = process.env[variable];
let app;
try {
    process.env[variable] = "build/test";
    const source = (await readFile(`test/${format}/score.janet`, "utf8")).replace("(daw/end 0.2)", "(daw/end 30)");
    await writeFile(entry, source);
    app = nativeEditor(entry);
    const url = await app.url;
    await withBrowser(async ({call, evaluate, wait, click, diagnostics}) => {
        await call("Page.navigate", {url});
        await wait('document.querySelector("#play")?.disabled === false');
        await click("#settings");
        await wait(`Array.from(document.querySelectorAll(".catalog-item")).some(e => e.textContent.includes("${title} test synth"))`);
        assert(await evaluate(`Array.from(document.querySelectorAll(".catalog-item")).some(e => e.textContent.includes("${title} test effect"))`));
        await evaluate('document.querySelector("#settings-dialog").close()');
        await click("#views");
        await click("#play");
        const root = `document.querySelector('.plugin[data-node="0"] .plugin-body > div')?.shadowRoot`;
        await wait(`${root}?.querySelectorAll('.parameter-controls input').length === ${format === 'clap' ? 130 : 3}`);
        const slider = `${root}.querySelector('input')`, value = `${root}.querySelector('output')`;
        await wait(`${value}.textContent === '0.75'`);
        await evaluate(`(() => { const s = ${slider}; for (let i = 1; i <= 400; i++) { s.value = i / 1000; s.dispatchEvent(new Event('input')); } s.dispatchEvent(new Event('change')); })()`);
        await wait(`${value}.textContent === '0.4'`);
        await click("#stop");
        await wait('document.querySelector("#stop").disabled');
        await evaluate(`(() => { const s = ${slider}; s.value = .6; s.dispatchEvent(new Event('input')); s.dispatchEvent(new Event('change')); })()`);
        await wait(`${value}.textContent === '0.6'`);
        const window = '.plugin[data-node="0"] button[aria-label="Toggle native UI"]';
        await click(window);
        await wait(`document.querySelector('${window}').getAttribute('aria-pressed') === 'true'`);
        await click(window);
        await wait(`document.querySelector('${window}').getAttribute('aria-pressed') === 'false'`);
        await evaluate(`document.querySelector('.plugin[data-node="0"]').open = true`);
        await wait(`${value}?.textContent === '0.6'`);
        await click("#play");
        await wait('!document.querySelector("#stop").disabled');
        await click("#stop");
        await wait('document.querySelector("#stop").disabled');
        assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
        assert.deepEqual(diagnostics, []);
    }, {graphics: true});
    console.log(`OK: ${title} catalog, shared parameter panel, rapid/stopped edits, native UI toggle and resumed playback`);
} finally {
    if (previous === undefined) delete process.env[variable]; else process.env[variable] = previous;
    try { await app?.close(); }
    finally { await rm(directory, {recursive: true, force: true}); }
}
