import assert from "node:assert/strict";
import {once} from "node:events";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const server = serve(0);
await once(server, "listening");
try {
    await withBrowser(async ({call, evaluate, diagnostics}) => {
        const page = process.argv[2] || "test/web.html";
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/${page}`});
        let result;
        for (let i = 0; i < 200; i++) {
            result = await evaluate('({text: document.querySelector("#status")?.textContent, disabled: document.querySelector("#run")?.disabled})');
            if (result.text?.startsWith("Error")) throw Error(result.text);
            if (result.disabled === false) break;
            await new Promise(resolve => setTimeout(resolve, 100));
        }
        assert.equal(result.disabled, false, JSON.stringify(diagnostics));
        const live = page === "test/live.html", seen = new Set();
        const expectedLines = live ? await evaluate('document.querySelector("#code").value.split("\\n").flatMap((line, i) => ["(section ", "(array/push items", "(note items left", "(note items key1"].some(text => line.includes(text)) ? [i + 1] : [])') : [];
        const {x, y} = await evaluate('(() => { const r = document.querySelector("#run").getBoundingClientRect(); return {x: r.x + r.width / 2, y: r.y + r.height / 2}; })()');
        await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1});
        await call("Input.dispatchMouseEvent", {type: "mouseReleased", x, y, button: "left", clickCount: 1});
        if (live) await evaluate('document.querySelector("#code").value = "# EDITED DRAFT\\n" + document.querySelector("#code").value');
        for (let i = 0; i < 500; i++) {
            await new Promise(resolve => setTimeout(resolve, 100));
            result = await evaluate('({text: document.querySelector("#status").textContent, disabled: document.querySelector("#run").disabled})');
            if (live) for (const line of await evaluate('[...document.querySelectorAll("#playing mark")].map(mark => parseInt(mark.textContent))')) seen.add(line);
            if (!result.disabled) break;
        }
        assert(result.text.startsWith("OK:"), result.text + "\n" + JSON.stringify(diagnostics));
        assert.equal(result.disabled, false, "Player cleanup did not complete");
        assert.deepEqual(diagnostics, []);
        if (live) {
            assert(expectedLines.length > 0);
            for (const line of expectedLines) assert(seen.has(line), `Event producer/caller at line ${line} was never highlighted`);
            assert.equal(await evaluate('document.querySelector("#playing").textContent.includes("EDITED DRAFT")'), false);
            assert.equal(await evaluate('document.querySelector("#code").value.startsWith("# EDITED DRAFT")'), true);
            assert.equal(await evaluate('document.querySelectorAll("#playing mark").length'), 0);
            console.log("OK: Polpo sections, note/drum/parameter producers and riff/chord callers highlighted; source snapshot preserved");
        }
        console.log(result.text);
    });
} finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
}
