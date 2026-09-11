import assert from "node:assert/strict";
import {spawn} from "node:child_process";
import {mkdtemp, rm} from "node:fs/promises";
import {tmpdir} from "node:os";
import {once} from "node:events";
import {serve} from "./server.mjs";

const server = serve(0);
await once(server, "listening");
const profile = await mkdtemp(tmpdir() + "/triccheballacche-");
const browser = spawn(process.env.CHROMIUM || "chromium", ["--headless", "--no-sandbox", "--disable-gpu",
    "--remote-debugging-port=0", "--user-data-dir=" + profile, "about:blank"], {stdio: ["ignore", "ignore", "pipe"]});
let socket, sequence = 0;
const pending = new Map(), diagnostics = [];
const deadline = setTimeout(() => browser.kill(), 60000);
try {
    const port = await new Promise((resolve, reject) => {
        browser.on("error", reject);
        browser.on("exit", () => reject(Error("Chromium exited before DevTools started")));
        browser.stderr.on("data", bytes => {
            const match = String(bytes).match(/DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)/);
            if (match) resolve(match[1]);
        });
    });
    const pages = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    socket = new WebSocket(pages.find(page => page.type === "page").webSocketDebuggerUrl);
    await once(socket, "open");
    socket.onmessage = ({data}) => {
        const event = JSON.parse(data);
        if (event.id) {
            const call = pending.get(event.id);
            pending.delete(event.id);
            event.error ? call.reject(Error(event.error.message)) : call.resolve(event.result);
        } else if (event.method === "Runtime.exceptionThrown") {
            diagnostics.push(event.params.exceptionDetails);
            if (process.env.DEBUG) console.error(JSON.stringify(event.params.exceptionDetails));
        } else if (process.env.DEBUG && event.method === "Runtime.consoleAPICalled")
            console.log(event.params.args.map(arg => arg.value ?? arg.description).join(" "));
    };
    socket.onclose = () => { for (const call of pending.values()) call.reject(Error("DevTools disconnected")); };
    const call = (method, params = {}) => new Promise((resolve, reject) => {
        const id = ++sequence;
        pending.set(id, {resolve, reject});
        socket.send(JSON.stringify({id, method, params}));
    });
    const evaluate = async expression => {
        const result = await call("Runtime.evaluate", {expression, returnByValue: true});
        assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
        return result.result.value;
    };
    await call("Runtime.enable");
    await call("Page.enable");
    await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/test/web.html`});
    let result;
    for (let i = 0; i < 200; i++) {
        result = await evaluate('({text: document.querySelector("#status")?.textContent, disabled: document.querySelector("#run")?.disabled})');
        if (result.text?.startsWith("Error")) throw Error(result.text);
        if (result.disabled === false) break;
        await new Promise(resolve => setTimeout(resolve, 100));
    }
    assert.equal(result.disabled, false, JSON.stringify(diagnostics));
    const {x, y} = await evaluate('(() => { const r = document.querySelector("#run").getBoundingClientRect(); return {x: r.x + r.width / 2, y: r.y + r.height / 2}; })()');
    await call("Input.dispatchMouseEvent", {type: "mousePressed", x, y, button: "left", clickCount: 1});
    await call("Input.dispatchMouseEvent", {type: "mouseReleased", x, y, button: "left", clickCount: 1});
    for (let i = 0; i < 300; i++) {
        await new Promise(resolve => setTimeout(resolve, 100));
        result = await evaluate('({text: document.querySelector("#status").textContent, disabled: document.querySelector("#run").disabled})');
        if (!result.disabled) break;
    }
    assert(result.text.startsWith("OK:"), result.text + "\n" + JSON.stringify(diagnostics));
    assert.equal(result.disabled, false, "Player cleanup did not complete");
    assert.deepEqual(diagnostics, []);
    console.log(result.text);
} finally {
    clearTimeout(deadline);
    socket?.close();
    const exited = browser.exitCode !== null || browser.signalCode !== null ? Promise.resolve() : once(browser, "exit");
    browser.kill();
    await exited;
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    await rm(profile, {recursive: true, force: true, maxRetries: 5, retryDelay: 100});
}
