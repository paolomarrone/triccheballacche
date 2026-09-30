import {createPlayerHost, attachPlayer, closePlayer} from "./player.js";
import {prepareBackground} from "./prepare.js";
import {addFile} from "./host.js";

export const saveLabel = "Download", saveTitle = "Download a copy of the score · Ctrl+S";
export const canUpload = true;
const options = new URLSearchParams(location.search);
const entry = options.get("score") || "examples/prog/polpo.janet";
const assets = new Map();
let library;
let host, player, transport = 0, failure = "", log = [];

export async function connect() {
    if (!crossOriginIsolated) throw Error("COOP/COEP headers required: run node test/server.mjs.");
    host = await createPlayerHost({printErr: message => log.push(message)});
    const url = new URL(options.get("project") || "../build/web/project.json", location.href);
    const response = await fetch(url, {cache: "no-store"});
    if (!response.ok) throw Error("Web catalog missing: run make web.");
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
    const entries = files.filter(file => file.path.endsWith(".perone/product.json") ||
        file.path.startsWith("examples/") && file.path.endsWith(".janet")).map(file => ({
            path: file.path, text: file.path.endsWith(".json") ? host.FS.readFile(host.perone.path(file.path), {encoding: "utf8"}) : undefined
        }));
    library = {paths: [url.href], files: entries};
    transport = host._transport_new();
    if (!transport) throw Error("Out of memory");
}

function query(op = "", args = []) {
    const values = Array.from({length: 6}, (_, i) => Number(args[i] ?? 0));
    const pointer = host.ccall("transport_json_web", "number",
        ["number", "string", "number", ...values.map(() => "number")], [transport, op, player?.time || 0, ...values]);
    if (!pointer) throw Error("Out of memory for transport state");
    try {
        const state = JSON.parse(host.UTF8ToString(pointer));
        if (state.view?.score) state.view.score.controlRevision = state.controlRevision;
        return {...state, ...state.view};
    } finally { host._free(pointer); }
}

async function stop() {
    if (player) await player.stop();
}

async function run(path, source, state, live) {
    if (live && state.queued) throw Error("Wait for the pending revision to become active");
    if (live && !(state.playing && state.live)) throw Error("Live updates require a playing live score");
    failure = "";
    log = [];
    let nextScore = 0, replacing = false;
    try {
        nextScore = await prepareBackground(host, path, source, 48000);
        if (live) {
            player.update(nextScore);
            host._web_score_free(nextScore);
            nextScore = 0;
            return {};
        }
        await stop();
        host.perone.deferred = true;
        try {
            if (host._web_score_activate(nextScore)) throw Error("Cannot activate plugin graph");
        } finally { host.perone.deferred = false; }
        replacing = true;
        player = undefined;
        await closePlayer(host);
        const owned = nextScore;
        nextScore = 0;
        const prepared = await attachPlayer(host, owned);
        player = prepared;
        await prepared.start();
        prepared.publish(transport);
        return query("score");
    } catch (error) {
        if (nextScore) host._web_score_free(nextScore);
        const diagnostics = log.join("\n");
        try {
            if (replacing) {
                player = undefined;
                await closePlayer(host);
            }
        } catch (cleanup) { throw new AggregateError([error, cleanup], `${error}\n${cleanup}`); }
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
    if (op === "library") return library;
    host._transport_collect(transport);
    const state = query();
    let result = {}, error = "", path = ["range", "origin", "automation", "listen", "seek"].includes(op) ? "" : args[0] || entry;
    try {
        if (op === "files") {
            path = host.perone.path(args[0] || ".");
            result.files = host.FS.readdir(path).filter(name => !name.startsWith(".")).map(name => ({
                name, directory: host.FS.isDir(host.FS.stat(`${path}/${name}`).mode)
            }));
        } else if (op === "import") {
            result.text = checkedText(args[1]);
            await addFile(host, path, new TextEncoder().encode(result.text));
        } else if (op === "listen") {
            const [version, track, flags] = args;
            if (!player || version !== state.revision) throw Error("Stale track view");
            player.listen(track, flags);
        } else if (["watch", "controls", "parameter", "message"].includes(op)) {
            const [version, node, ...values] = args;
            if (!player || version !== state.controlRevision) throw Error("Stale plugin view");
            if (op === "watch") {
                if (!["off", "web"].includes(values[0])) throw Error("Invalid view type");
                await player.control("watch", node, values[0] === "web");
            } else {
                result = await player.control(op, node, ...values);
            }
        } else if (op === "range" || op === "origin" || op === "automation") result = query(op, args);
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
        } else if (op === "run" || op === "update") result = await run(path, checkedText(args[1]), state, op === "update");
        else if (op === "seek") {
            const seconds = Number(args[0]);
            if (!player) throw Error("Run a score before seeking");
            if (!player.canSeek(seconds)) throw Error("Position outside score");
            const resume = state.playing;
            await stop();
            await player.seek(seconds);
            failure = "";
            if (resume) await player.start();
        } else if (op === "play") {
            if (!player) throw Error("Run a score before playing");
            if (!state.playing) {
                if (player.time >= player.duration) await player.seek(0);
                await player.start();
                failure = "";
            }
        } else if (op === "stop") await stop();
        else if (op === "score") result = query("score");
        else if (op === "status") {
            if (player && state.playing) {
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
            result = query("status");
        } else throw Error("Unknown command: " + op);
    } catch (cause) { error = String(cause.message || cause); }
    return {...query(), ...result, path, error: error || result.error || ""};
}

export async function close() {
    if (!host) return;
    await closePlayer(host);
    player = undefined;
    if (transport) host._transport_free_web(transport);
    transport = 0;
}

export function uiUrl(node) {
    const url = assets.get(host.perone.path(node.bundle + "/" + node.product.ui.web));
    if (!url) throw Error("UI missing from the catalog: rebuild with make web");
    return url;
}
