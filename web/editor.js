import {createPlayerHost, preparePlayer, closePlayer} from "./player.js";
import {addFile} from "./host.js";

export const nativeViews = false, views = true, saveLabel = "Scarica", saveTitle = "Scarica una copia della partitura · Ctrl+S";
const options = new URLSearchParams(location.search);
const entry = options.get("score") || "examples/prog/polpo.janet";
const assets = new Map();
let host, player, view = 0, revision = 0, time = 0, watched = -1, failure = "", log = [];

export async function connect() {
    if (!crossOriginIsolated) throw Error("Servono gli header COOP/COEP: avvia node test/server.mjs.");
    host = await createPlayerHost({printErr: message => log.push(message)});
    const url = new URL(options.get("project") || "../build/web/project.json", location.href);
    const response = await fetch(url, {cache: "no-store"});
    if (!response.ok) throw Error("Catalogo web assente: esegui make web-editor.");
    const files = await response.json(), paths = new Set();
    for (const file of files) {
        const path = host.perone.path(file.path);
        if (paths.has(path)) throw Error("Percorso duplicato nel catalogo: " + file.path);
        paths.add(path);
        assets.set(path, new URL(file.url, url).href);
    }
    // Finish every preload before exposing the runtime, including on failure.
    const loaded = await Promise.allSettled(files.map(async file => {
        if (file.asset) return;
        const response = await fetch(new URL(file.url, url), {cache: "no-store"});
        if (!response.ok) throw Error("Impossibile caricare " + file.path);
        await addFile(host, file.path, new Uint8Array(await response.arrayBuffer()));
    }));
    const failed = loaded.find(result => result.status === "rejected");
    if (failed) throw failed.reason;
}

function query(op, args = []) {
    const values = Array.from({length: 6}, (_, i) => Number(args[i] ?? 0));
    const pointer = host.ccall("score_view_json", "number",
        ["number", "number", "string", ...values.map(() => "number")], [view, revision, op, ...values]);
    if (!pointer) throw Error("Memoria insufficiente per la proiezione");
    try { return JSON.parse(host.UTF8ToString(pointer)); }
    finally { host._free(pointer); }
}

async function stop() {
    if (player) time = player.time;
    // Clear the JS player even on failure; closePlayer retains its host lock and permits cleanup retries.
    player = undefined;
    watched = -1;
    await closePlayer(host);
}

async function run(path, source) {
    await stop();
    const previousTime = time;
    failure = "";
    log = [];
    try {
        const prepared = await preparePlayer(host, path, 48000, source);
        await prepared.start();
        const next = prepared.takeView();
        if (!next) throw Error("Proiezione della partitura assente");
        host._view_free(view);
        view = next;
        player = prepared;
        ++revision;
        time = 0;
        return query("score");
    } catch (error) {
        const diagnostics = log.join("\n");
        try { await stop(); }
        catch (cleanup) { throw new AggregateError([error, cleanup], `${error}\n${cleanup}`); }
        finally { time = previousTime; }
        throw Error(diagnostics || String(error));
    }
}

function checkedText(text) {
    if (typeof text !== "string" || text.includes("\0") || new TextEncoder().encode(text).length > 8 * 1024 * 1024)
        throw Error("La partitura deve essere testo senza NUL, massimo 8 MiB");
    return text;
}

// The shared controller serializes commands, including asynchronous player cleanup.
export async function command(op, ...args) {
    let result = {}, error = "", path = op === "range" || op === "note" ? "" : args[0] || entry;
    try {
        if (["watch", "controls", "parameter", "message"].includes(op)) {
            const [version, node, ...values] = args;
            if (!player || version !== revision) throw Error("Vista plugin scaduta");
            if (op === "watch") {
                if (watched >= 0) await player.control("watch", watched, false);
                watched = -1;
                if (node >= 0) {
                    await player.control("watch", node, true);
                    watched = node;
                }
            } else {
                if (node !== watched) throw Error("Vista plugin non collegata");
                result = await player.control(op, node, ...values);
            }
        } else if (op === "range" || op === "note") result = query(op, args);
        else if (op === "open") {
            try { result.text = checkedText(host.FS.readFile(host.perone.path(path), {encoding: "utf8"})); }
            catch { throw Error("File non presente nel progetto web: " + path); }
        } else if (op === "save") {
            const source = checkedText(args[1]);
            await addFile(host, path, new TextEncoder().encode(source));
            const url = URL.createObjectURL(new Blob([source], {type: "text/plain;charset=utf-8"}));
            const link = document.createElement("a");
            link.href = url; link.download = path.split("/").at(-1);
            link.click();
            setTimeout(() => URL.revokeObjectURL(url), 1000);
        } else if (op === "run") result = await run(path, checkedText(args[1]));
        else if (op === "stop") await stop();
        else if (op === "status") {
            if (player) {
                try {
                    const state = player.status;
                    if (state < 0) failure = "Errore durante la riproduzione";
                    if (state || player.context.state === "closed") await stop();
                } catch (error) {
                    failure = String(error);
                    await stop();
                }
            }
            error = failure;
            result = query("status", [player ? player.time : time, Boolean(player)]);
        } else throw Error("Comando sconosciuto: " + op);
    } catch (cause) { error = String(cause.message || cause); }
    return {...result, path, revision, time: player ? player.time : time, playing: Boolean(player), error: error || result.error || ""};
}

export async function close() {
    if (!host) return;
    try { await stop(); }
    finally { host._view_free(view); view = 0; }
}

export function uiUrl(node) {
    const url = assets.get(host.perone.path(node.bundle + "/" + node.product.ui.web));
    if (!url) throw Error("UI assente dal catalogo: rigenera con make web-editor");
    return url;
}
