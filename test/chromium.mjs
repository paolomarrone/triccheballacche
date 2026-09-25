import assert from "node:assert/strict";
import {spawn} from "node:child_process";
import {mkdtemp, rm} from "node:fs/promises";
import {tmpdir} from "node:os";
import {once} from "node:events";

// One isolated browser and DevTools connection, always closed even when a check fails.
export async function withBrowser(run) {
    const profile = await mkdtemp(tmpdir() + "/triccheballacche-");
    const browser = spawn(process.env.CHROMIUM || "chromium", ["--headless", "--no-sandbox", "--disable-gpu",
        "--remote-debugging-port=0", "--user-data-dir=" + profile, "about:blank"], {stdio: ["ignore", "ignore", "pipe"]});
    let socket, sequence = 0;
    const pending = new Map(), diagnostics = [];
    const deadline = setTimeout(() => browser.kill(), 60000);
    try {
        const port = await new Promise((resolve, reject) => {
            let output = "";
            browser.on("error", reject);
            browser.on("exit", () => reject(Error("Chromium exited before DevTools started")));
            browser.stderr.on("data", bytes => {
                output += bytes;
                const match = output.match(/DevTools listening on ws:\/\/127\.0\.0\.1:(\d+)\//);
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
            const result = await call("Runtime.evaluate", {expression, returnByValue: true, awaitPromise: true});
            assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
            return result.result.value;
        };
        const wait = async (expression, timeout = 15000) => {
            const until = Date.now() + timeout;
            while (Date.now() < until) {
                const value = await evaluate(expression);
                if (value) return value;
                await new Promise(resolve => setTimeout(resolve, 50));
            }
            const state = await evaluate('({error: document.querySelector("#errors")?.textContent, status: document.querySelector("#state")?.textContent || document.querySelector("#status")?.textContent, time: document.querySelector("#time")?.value})');
            throw Error(`Timed out: ${expression}\n${JSON.stringify(state)}\n${JSON.stringify(diagnostics)}`);
        };
        const click = async selector => {
            const target = `document.querySelector(${JSON.stringify(selector)})`;
            await wait(`${target} && !${target}.disabled`);
            const {x, y} = await evaluate(`(() => {
                const node = ${target}; node.scrollIntoView({block: "nearest"});
                const r = node.getBoundingClientRect(); return {x: r.x + r.width / 2, y: r.y + r.height / 2};
            })()`);
            for (const type of ["mousePressed", "mouseReleased"])
                await call("Input.dispatchMouseEvent", {type, x, y, button: "left", clickCount: 1});
        };
        const set = (selector, value) => evaluate(`(() => {
            const node = document.querySelector(${JSON.stringify(selector)}); node.value = ${JSON.stringify(value)};
            for (const type of ["input", "change"]) node.dispatchEvent(new Event(type, {bubbles: true}));
        })()`);
        const key = async (key, code = key) => {
            const windowsVirtualKeyCode = {Enter: 13, Escape: 27}[key] || 0;
            for (const type of ["keyDown", "keyUp"])
                await call("Input.dispatchKeyEvent", {type, key, code, windowsVirtualKeyCode});
        };
        await call("Runtime.enable");
        await call("Page.enable");
        return await run({call, evaluate, wait, click, set, key, diagnostics});
    } finally {
        clearTimeout(deadline);
        socket?.close();
        if (browser.pid && browser.exitCode === null && browser.signalCode === null) {
            const exited = once(browser, "exit");
            browser.kill();
            await exited;
        }
        await rm(profile, {recursive: true, force: true, maxRetries: 5, retryDelay: 100});
    }
}
