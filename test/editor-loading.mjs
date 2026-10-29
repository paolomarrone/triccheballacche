import assert from "node:assert/strict";
import {once} from "node:events";
import {mkdtemp, readFile, writeFile, rm} from "node:fs/promises";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const directory = await mkdtemp("build/test/editor-loading-");
const entry = `${directory}/score.janet`, catalog = `${directory}/project.json`;
const binary = "build/test/fixture.perone/wasm32/fixture.wasm", missing = `${directory}/missing.wasm`;
const source = `(def tone (daw/plugin {:format :perone :id "fixture"} {:gain 0.02}))
(daw/output (daw/track tone))
(daw/note tone 0 120 60)
(daw/end 120)
`;
const server = serve(0);
await once(server, "listening");
try {
    await writeFile(entry, source);
    await writeFile(catalog, JSON.stringify([
        ...[entry, "lib/pattern.janet", "lib/trace.janet", "build/test/fixture.perone/product.json", binary]
            .map(path => ({path, url: "/" + path})),
        {path: "build/test/missing.perone/product.json", url: "/build/test/fixture.perone/product.json"},
        {path: "build/test/missing.perone/wasm32/fixture.wasm", url: "/" + missing}
    ]));
    await withBrowser(async ({call, evaluate, wait, click, set, diagnostics}) => {
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`});
        await wait('document.querySelector("#play")?.disabled === false');
        const downloads = path => evaluate(`performance.getEntriesByType('resource')
            .filter(entry => new URL(entry.name).pathname === ${JSON.stringify("/" + path)}).length`);
        assert.equal(await downloads(binary), 0);
        assert.equal(await downloads(missing), 0, "An unused missing DSP must not prevent startup");
        await click("#play");
        await wait('Number(document.querySelector("#time").value) > 0.1');
        assert.equal(await downloads(binary), 1);
        assert.equal(await downloads(missing), 0);
        const revision = await evaluate('Number(document.querySelector("#timeline").dataset.revision)');
        await click("#settings");
        await click("#settings-plugins-tab");
        assert.match(await evaluate('document.querySelector(\'.catalog-item[data-path="build/test/fixture.perone"]\').textContent'), /1 in use/);
        await click('#settings-dialog [data-close]');
        await set("#code", source.replace(':id "fixture"', ':id "unknown"'));
        await click("#apply");
        await wait('document.querySelector("#errors").textContent.includes("plugin not found: unknown")');
        assert.equal(await evaluate('Number(document.querySelector("#timeline").dataset.revision)'), revision);
        await set("#code", source.replace(':id "fixture"', ':path "build/test/missing.perone"'));
        await click("#apply");
        await wait('document.querySelector("#errors").textContent.includes("Cannot load")');
        assert.equal(await downloads(missing), 1);
        assert.equal(await evaluate('Number(document.querySelector("#timeline").dataset.revision)'), revision);
        const time = await evaluate('Number(document.querySelector("#time").value)');
        await wait(`Number(document.querySelector('#time').value) > ${time + .1}`);
        assert.equal(await evaluate('document.querySelector("#play").getAttribute("aria-label")'), "Stop",
            "A download failure must leave the existing audio running");
        await writeFile(missing, await readFile(binary));
        await click("#apply");
        await wait('!document.querySelector("#play").disabled');
        assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
        assert(await evaluate(`Number(document.querySelector('#timeline').dataset.revision) > ${revision}`));
        assert.equal(await downloads(missing), 2, "Failed downloads can be retried");
        await set("#code", source);
        await click("#apply");
        await wait('!document.querySelector("#play").disabled');
        assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
        assert.equal(await downloads(binary), 1, "A previously compiled DSP is reused");
        await click('#play[aria-label="Stop"]');
        assert.deepEqual(diagnostics, []);
        await call("Page.navigate", {url: "about:blank"});
        console.log("OK: DSP downloads follow the prepared graph; unused modules, failures, retries and reuse preserve the live player");
    });
} finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    await rm(directory, {recursive: true, force: true});
}
