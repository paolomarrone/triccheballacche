import {createPlayerHost, prepareScore, attachPlayer, closePlayer} from "./player.js";
import {addFile} from "./host.js";

export const saveLabel = "Download", saveTitle = "Download a copy of the score · Ctrl+S";
const options = new URLSearchParams(location.search);
const entry = options.get("score") || "examples/prog/polpo.janet";
const assets = new Map();
let host, player, playing = false, view = 0, revision = 0, time = 0, failure = "", log = [];

export async function connect() {
    if (!crossOriginIsolated) throw Error("COOP/COEP headers required: run node test/server.mjs.");
    host = await createPlayerHost({printErr: message => log.push(message)});
    const url = new URL(options.get("project") || "../build/web/project.json", location.href);
    const response = await fetch(url, {cache: "no-store"});
    if (!response.ok) throw Error("Web catalog missing: run make web-editor.");
    const files = await response.json(), paths = new Set();
    for (const file of files) {
        const path = host.perone.path(file.path);
        if (paths.has(path)) throw Error("Duplicate catalog path: " + file.path);
        paths.add(path);
        assets.set(path, new URL(file.url, url).href);
    }
    // Finish every preload before exposing the runtime, including on failure.
    const loaded = await Promise.allSettled(files.map(async file => {
        if (file.asset) return;
        const response = await fetch(new URL(file.url, url), {cache: "no-store"});
        if (!response.ok) throw Error("Cannot load " + file.path);
        await addFile(host, file.path, new Uint8Array(await response.arrayBuffer()));
    }));
    const failed = loaded.find(result => result.status === "rejected");
    if (failed) throw failed.reason;
}

function query(op, args = []) {
    const values = Array.from({length: 6}, (_, i) => Number(args[i] ?? 0));
    const pointer = host.ccall("score_view_json", "number",
        ["number", "number", "string", ...values.map(() => "number")], [view, revision, op, ...values]);
    if (!pointer) throw Error("Out of memory for score projection");
    try { return JSON.parse(host.UTF8ToString(pointer)); }
    finally { host._free(pointer); }
}

async function stop() {
    playing = false;
    if (player) {
        await player.stop();
        time = player.time;
    }
}

async function run(path, source) {
    await stop();
    const previousTime = time;
    failure = "";
    log = [];
    let nextScore = 0, replacing = false;
    try {
        nextScore = prepareScore(host, path, 48000, source);
        replacing = true;
        player = undefined;
        await closePlayer(host);
        const owned = nextScore;
        nextScore = 0;
        const prepared = await attachPlayer(host, owned);
        player = prepared;
        await prepared.start();
        const next = prepared.takeView();
        if (!next) throw Error("Score projection missing");
        host._view_free(view);
        view = next;
        playing = true;
        ++revision;
        time = 0;
        return query("score");
    } catch (error) {
        if (nextScore) host._score_free(nextScore);
        const diagnostics = log.join("\n");
        try {
            if (replacing) {
                player = undefined;
                playing = false;
                await closePlayer(host);
            }
        } catch (cleanup) { throw new AggregateError([error, cleanup], `${error}\n${cleanup}`); }
        finally { time = previousTime; }
        throw Error(diagnostics || String(error));
    }
}

function checkedText(text) {
    if (typeof text !== "string" || text.includes("\0") || new TextEncoder().encode(text).length > 8 * 1024 * 1024)
        throw Error("Score must be text without NUL bytes, at most 8 MiB");
    return text;
}

// The shared controller serializes commands, including asynchronous player cleanup.
export async function command(op, ...args) {
    let result = {}, error = "", path = ["range", "note", "listen"].includes(op) ? "" : args[0] || entry;
    try {
        if (op === "listen") {
            const [version, track, flags] = args;
            if (!player || version !== revision) throw Error("Stale track view");
            player.listen(track, flags);
        } else if (["watch", "controls", "parameter", "message"].includes(op)) {
            const [version, node, ...values] = args;
            if (!player || version !== revision) throw Error("Stale plugin view");
            if (op === "watch") {
                if (!["off", "web"].includes(values[0])) throw Error("Invalid view type");
                await player.control("watch", node, values[0] === "web");
            } else {
                result = await player.control(op, node, ...values);
            }
        } else if (op === "range" || op === "note") result = query(op, args);
        else if (op === "open") {
            try { result.text = checkedText(host.FS.readFile(host.perone.path(path), {encoding: "utf8"})); }
            catch { throw Error("File missing from the web project: " + path); }
        } else if (op === "save") {
            const source = checkedText(args[1]);
            await addFile(host, path, new TextEncoder().encode(source));
            const url = URL.createObjectURL(new Blob([source], {type: "text/plain;charset=utf-8"}));
            const link = document.createElement("a");
            link.href = url; link.download = path.split("/").at(-1);
            link.click();
            setTimeout(() => URL.revokeObjectURL(url), 1000);
        } else if (op === "run") result = await run(path, checkedText(args[1]));
        else if (op === "play") {
            if (!player) throw Error("Run a score before playing");
            await stop();
            try { await player.restart(); }
            catch (error) { await stop(); throw error; }
            playing = true;
            time = 0;
            failure = "";
        } else if (op === "stop") await stop();
        else if (op === "status") {
            if (player && playing) {
                try {
                    const state = player.status;
                    if (state < 0) failure = "Playback error";
                    if (state || player.context.state === "closed") await stop();
                } catch (error) {
                    failure = String(error);
                    await stop();
                }
            }
            error = failure;
            result = query("status", [playing ? player.time : time, playing]);
        } else throw Error("Unknown command: " + op);
    } catch (cause) { error = String(cause.message || cause); }
    return {...result, path, revision, time: playing ? player.time : time, playing, prepared: Boolean(player), error: error || result.error || ""};
}

export async function close() {
    if (!host) return;
    playing = false;
    player = undefined;
    try { await closePlayer(host); }
    finally { host._view_free(view); view = 0; }
}

export function uiUrl(node) {
    const url = assets.get(host.perone.path(node.bundle + "/" + node.product.ui.web));
    if (!url) throw Error("UI missing from the catalog: rebuild with make web-editor");
    return url;
}
