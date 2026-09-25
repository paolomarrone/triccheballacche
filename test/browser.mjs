import assert from "node:assert/strict";
import {once} from "node:events";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const server = serve(0);
await once(server, "listening");
try {
    await withBrowser(async ({call, wait, click, diagnostics}) => {
        const page = process.argv[2] || "test/web.html";
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/${page}`});
        await wait('document.querySelector("#run")?.disabled === false', 20000);
        await click("#run");
        const result = await wait('!document.querySelector("#run").disabled && document.querySelector("#status").textContent', 50000);
        assert(result.startsWith("OK:"), result + "\n" + JSON.stringify(diagnostics));
        assert.deepEqual(diagnostics, []);
        console.log(result);
    });
} finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
}
