import {midiDevices} from "./midi.js";
import {encodeState, decodeState} from "../editor/preset.js";
import {download} from "../editor/files.js";

import {createPlayerHost, attachPlayer, closePlayer} from "./player.js";
import {prepareBackground} from "./prepare.js";
import {addFile} from "./host.js";

export const saveLabel = "Download", saveTitle = "Download a copy of the score · Ctrl+S";
export const canUpload = true;
const options = new URLSearchParams(location.search);
const entry = options.get("score") || "examples/polpo.janet";
const assets = new Map();
let library;
let host, player, transport = 0, sampleRate = 48000, failure = "", midiFailed = false, log = [];
// Device callbacks report errors; the command loop owns asynchronous player changes.
const midi = midiDevices(() => player, error => { failure = String(error); midiFailed = true; });

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
        if (file.asset || file.path.endsWith(".wasm")) return;
        await addFile(host, file.path, await fetchFile(file.path));
    }));
    const failed = loaded.find(result => result.status === "rejected");
    if (failed) throw failed.reason;
    const entries = files.filter(file => file.path.endsWith(".perone/product.json") ||
        file.path.startsWith("examples/") && file.path.endsWith(".janet")).map(file => ({
            path: file.path, text: file.path.endsWith(".json") ? host.FS.readFile(host.perone.path(file.path), {encoding: "utf8"}) : undefined
        }));
    library = {sources: [{format: "perone", paths: [url.href]}], editable: false, files: entries};
    transport = host._transport_new();
    if (!transport) throw Error("Out of memory");
}

async function fetchFile(path) {
    const url = assets.get(host.perone.path(path));
    if (!url) throw Error("File missing from the web catalog: " + path);
    const response = await fetch(url, {cache: "no-store"});
    if (!response.ok) throw Error("Cannot load " + path);
    return new Uint8Array(await response.arrayBuffer());
}

function query(op = "", args = []) {
    const values = Array.from({length: 6}, (_, i) => Number(args[i] ?? 0));
    const pointer = host.ccall("transport_json_web", "number",
        ["number", "string", "number", ...values.map(() => "number")], [transport, op, player?.time || 0, ...values]);
    if (!pointer) throw Error("Out of memory for transport state");
    try {
        const state = JSON.parse(host.UTF8ToString(pointer));
        if (state.view?.score) state.view.score.controlRevision = state.controlRevision;
        return {...state, ...state.view, audio: !!player?.active};
    } finally { host._free(pointer); }
}

async function pause() {
    try { midi.stop(); }
    finally { if (player) await player.pause(); }
}

async function start(playing) {
    try {
        await player.monitor();
        if (playing && player.duration) await player.start();
        midi.start();
        failure = "";
    } catch (error) {
        try { await pause(); }
        catch (cleanup) { throw new AggregateError([error, cleanup], `${error}; ${cleanup}`); }
        throw error;
    }
}

async function run(path, source, state, live, playing) {
    if (live && state.queued) throw Error("Wait for the pending revision to become active");
    if (live && !(state.playing && state.live)) throw Error("Live updates require a playing live score");
    failure = "";
    log = [];
    let nextScore = 0, replacing = false;
    try {
        nextScore = await prepareBackground(host, path, source, sampleRate);
        if (live) {
            player.update(nextScore);
            host._web_score_free(nextScore);
            nextScore = 0;
            return {};
        }
        host.perone.deferred = true;
        try {
            if (host._web_score_activate(nextScore)) throw Error("Cannot activate plugin graph");
        } finally { host.perone.deferred = false; }
        await host.perone.loadPrepared(fetchFile);
        await pause();
        await midi.detach();
        replacing = true;
        player = undefined;
        await closePlayer(host);
        const owned = nextScore;
        nextScore = 0;
        const prepared = await attachPlayer(host, owned);
        player = prepared;
        prepared.publish(transport);
        await start(playing);
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
    if (op === "audio-info") return {
        sampleRate: player?.context.sampleRate || sampleRate, output: "Browser default",
        latency: player ? player.context.baseLatency + (player.context.outputLatency || 0) : 0
    };
    if (op === "midi-devices") return midi.list();
    if (op === "midi-refresh") return midi.list();
    if (midiFailed) {
        midiFailed = false;
        try { await pause(); }
        catch (error) { failure += `; ${error}`; }
    }
    host._transport_collect(transport);
    const state = query();
    let result = {}, error = "", path = ["range", "origin", "automation", "listen", "seek", "audio", "sample-rate"].includes(op) ? "" : args[0] || entry;
    try {
        if (op === "midi-connect") {
            const [version, id, node] = args;
            if (!player || version !== state.controlRevision) throw Error("Stale MIDI connection");
            const view = query("score").score;
            const device = (await midi.list()).devices.find(p => p.id === id);
            if (!device || !Number.isInteger(node) || node < -1 || node >= view.nodes.length ||
                node >= 0 && !view.nodes[node][device.input ? "midiIn" : "midiOut"]) throw Error("Invalid MIDI connection");
            await pause();
            try { result = await midi.connect(id, node); }
            finally { if (state.audio) await start(state.playing); }
        } else if (op === "files") {
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
        } else if (op === "state-save" || op === "state-load") {
            const [version, node, data] = args;
            if (!player || version !== state.controlRevision) throw Error("Stale plugin view");
            const bytes = op === "state-load" ? decodeState(data) : undefined;
            await pause();
            try {
                const saved = await player.state(node, bytes);
                if (saved) result.data = encodeState(saved);
            } finally { if (state.audio) await start(false); }
        } else if (["watch", "controls", "parameter", "message"].includes(op)) {
            const [version, node, ...values] = args;
            if (!player || version !== state.controlRevision) throw Error("Stale plugin view");
            if (op === "watch") {
                if (!["off", "web"].includes(values[0])) throw Error("Invalid view type");
                await player.control("watch", node, values[0] === "web");
            } else if (op === "controls") {
                result.controls = await player.controls(node);
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
            download(path.split("/").at(-1), source, "text/plain;charset=utf-8");
        } else if (["run", "prepare", "update"].includes(op))
            result = await run(path, checkedText(args[1]), state, op === "update", op === "run" || state.playing);
        else if (op === "sample-rate") {
            const rate = args[0];
            if (state.audio) throw Error("Turn the engine off before changing sample rate");
            if (!Number.isInteger(rate) || rate < 1 || rate > 384000) throw Error("Sample rate must be within 1..384000 Hz");
            if (rate !== sampleRate) {
                await midi.detach();
                player = undefined;
                await closePlayer(host);
                sampleRate = rate;
                failure = "";
            }
        } else if (op === "audio") {
            if (!player) throw Error("Prepare a score before enabling audio");
            if (args[0] === 0) await pause();
            else if (args[0] === 1) await start(state.playing);
            else throw Error("Invalid audio state");
        } else if (op === "seek") {
            const seconds = Number(args[0]);
            if (!player) throw Error("Prepare a score before seeking");
            if (!player.canSeek(seconds)) throw Error("Position outside score");
            await pause();
            await player.seek(seconds);
            failure = "";
            if (state.audio) await start(state.playing);
        } else if (op === "play") {
            if (!player) throw Error("Prepare a score before playing");
            if (!state.playing) {
                if (player.duration && player.time >= player.duration) {
                    await pause();
                    await player.seek(0);
                }
                await start(true);
            }
        } else if (op === "stop") { if (player) await player.stop(); }
        else if (op === "score") result = query("score");
        else if (op === "status") {
            if (player) {
                try {
                    const status = player.status;
                    if (status < 0 || player.context.state === "closed") {
                        failure = "Audio error";
                        await pause();
                    } else if (status === 1) await player.stop();
                } catch (error) {
                    failure = String(error);
                    await pause();
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
    let failure;
    try { await midi.detach(); }
    catch (error) { failure = error; }
    await closePlayer(host); // A failed close retains the transport for a safe retry.
    player = undefined;
    if (transport) host._transport_free_web(transport);
    transport = 0;
    if (failure) throw failure;
}

export function uiUrl(node) {
    const url = assets.get(host.perone.path(node.bundle + "/" + node.product.ui.web));
    if (!url) throw Error("UI missing from the catalog: rebuild with make web");
    return url;
}

export async function exportPreset(request, name, text) {
    download(name, text, "application/json");
}
