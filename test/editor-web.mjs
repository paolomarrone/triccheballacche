import assert from "node:assert/strict";
import {writeFile, readFile, mkdtemp, rm} from "node:fs/promises";
import {resolve, basename} from "node:path";
import {once} from "node:events";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const entry = process.argv[2] || "examples/prog/polpo.janet";
const downloads = await mkdtemp("build/editor-downloads-");
const server = serve(0);
await once(server, "listening");
try {
    await withBrowser(async ({call, evaluate, diagnostics}) => {
        const wait = async condition => {
            for (let i = 0; i < 300; ++i) {
                if (await evaluate(condition)) return;
                await new Promise(resolve => setTimeout(resolve, 50));
            }
            throw Error(`Timed out: ${condition}\n${await evaluate('document.querySelector("#errors").textContent')}\n${JSON.stringify(diagnostics)}`);
        };
        const click = async id => {
            await wait(`!document.querySelector('#${id}').disabled`);
            const {x, y} = await evaluate(`(() => { const r = document.querySelector('#${id}').getBoundingClientRect(); return {x:r.x + r.width/2,y:r.y + r.height/2}; })()`);
            await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1});
            await call("Input.dispatchMouseEvent", {type: "mouseReleased", x, y, button: "left", clickCount: 1});
        };
        const set = async (id, value) => evaluate(`(() => {
            const node = document.querySelector('#${id}'); node.value = ${JSON.stringify(value)};
            node.dispatchEvent(new Event('input')); node.dispatchEvent(new Event('change'));
        })()`);
        await call("Browser.setDownloadBehavior", {behavior: "allow", downloadPath: resolve(downloads)});
        await call("Emulation.setDeviceMetricsOverride", {width: 1000, height: 760, deviceScaleFactor: 1, mobile: false});
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/editor/index.html?score=${encodeURIComponent(entry)}`});
        await wait('document.querySelector("#run")?.disabled === false');
        assert(await evaluate('crossOriginIsolated && !globalThis.webui'));
        assert.equal(await evaluate('document.querySelector("#save").textContent'), "Scarica");
        assert.equal(await evaluate('document.querySelector("#plugin-views").getBoundingClientRect().width'), 0);
        assert.equal(await evaluate('getComputedStyle(document.querySelector("#sheet")).display'), "flex");
        const source = await evaluate('document.querySelector("#code").value');
        await click("run");
        await wait('!document.querySelector("#stop").disabled');
        await wait('Number(document.querySelector("#notes").dataset.notes) > 0');
        const revision = await evaluate('document.querySelector("#timeline").dataset.revision');
        await evaluate(`(() => { const code = document.querySelector('#code'); code.scrollTop = 60 * 20; code.dispatchEvent(new Event('scroll')); })()`);
        await wait('document.querySelector("#marks").childElementCount > 0');
        assert.equal(await evaluate('document.querySelector("#errors").hidden'), true);
        const screenshot = await call("Page.captureScreenshot", {format: "png"});
        await writeFile(`build/editor-web-${basename(entry, ".janet")}.png`, Buffer.from(screenshot.data, "base64"));
        await set("code", "# draft\n" + source);
        assert.equal(await evaluate('document.querySelector("#marks").childElementCount'), 0);
        await click("stop");
        await wait('document.querySelector("#state").textContent === "Fermo"');
        assert.equal(await evaluate('document.querySelector("#timeline").dataset.revision'), revision);
        const size = await evaluate('[document.querySelector("#notes").width,document.querySelector("#notes").height]');
        await set("view-start", 1e9);
        await wait('document.querySelector("#notes").dataset.notes === "0"');
        assert.deepEqual(await evaluate('[document.querySelector("#notes").width,document.querySelector("#notes").height]'), size);
        await set("view-start", 0);
        await wait('Number(document.querySelector("#notes").dataset.notes) > 0');
        await set("code", "(error \"errore wasm 音\")");
        await click("run");
        await wait('document.querySelector("#errors").textContent.includes("errore wasm 音")');
        assert.equal(await evaluate('document.querySelector("#timeline").dataset.revision'), revision);
        assert(await evaluate('Number(document.querySelector("#notes").dataset.notes) > 0'));
        await set("code", source); // Restore the exact unsaved score.
        await click("run");
        await wait(`Number(document.querySelector("#timeline").dataset.revision) > ${revision}`);
        await click("stop");
        await wait('document.querySelector("#state").textContent === "Fermo"');
        assert.equal(await evaluate('document.querySelector("#errors").hidden'), true);
        const downloaded = source + "\n# Unicode 音\n";
        await set("code", downloaded);
        await click("save");
        await wait('!document.querySelector("#modified").textContent');
        let contents;
        for (let i = 0; i < 100; ++i) {
            try { contents = await readFile(`${downloads}/${basename(entry)}`, "utf8"); break; }
            catch { await new Promise(resolve => setTimeout(resolve, 20)); }
        }
        assert.equal(contents, downloaded);
        assert.equal(await readFile(entry, "utf8"), source, "Download must not modify server files");
        await click("open");
        await wait('!document.querySelector("#run").disabled');
        assert.equal(await evaluate('document.querySelector("#code").value'), downloaded, "Downloads also update the session filesystem");
        await set("path", "examples/absent.janet");
        await click("open");
        await wait('document.querySelector("#errors").textContent.includes("assente") || document.querySelector("#errors").textContent.includes("non presente")');
        assert.equal(await evaluate('document.querySelector("#code").value'), downloaded);
        assert.deepEqual(diagnostics, []);
        await call("Page.navigate", {url: "about:blank"});
        console.log(`OK: shared editor in pure Wasm, ${entry}, bounded timeline, source tracking, drafts, stop, diagnostics, recovery and downloads`);
    });
} finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    await rm(downloads, {recursive: true, force: true});
}
