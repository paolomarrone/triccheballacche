import {createPlayerHost, preparePlayer, closePlayer} from "../web/player.js";
import {addFile} from "../web/host.js";
import {traceScore, readTrace, activeLines} from "./trace-host.mjs";

const run = document.querySelector("#run"), stop = document.querySelector("#stop");
const status = document.querySelector("#status"), time = document.querySelector("#time");
const code = document.querySelector("#code"), playing = document.querySelector("#playing");
const path = "examples/prog/polpo.janet";
let host, player, trace, animation, startedAt, stopping = false;
let rows = [], marked = new Set(), reportSummary = "";

function highlight(lines) {
    // Keep references to the text rows; wrapping/unwrapping never changes their text or coordinates.
    for (let i = 0; i < rows.length; i++) {
        const row = rows[i], active = lines.has(i + 1), parent = row.parentElement;
        if (active && parent?.tagName !== "MARK") {
            const mark = document.createElement("mark");
            row.replaceWith(mark);
            mark.append(row);
        } else if (!active && parent?.tagName === "MARK") parent.replaceWith(row);
    }
    for (const line of lines) marked.add(line);
}

async function finish(message) {
    if (stopping) return;
    stopping = true;
    stop.disabled = true;
    cancelAnimationFrame(animation);
    try {
        await closePlayer(host);
        const observed = [...marked].sort((a, b) => a - b).join(", ");
        status.textContent = `${message}\n${reportSummary}\nRighe evidenziate: ${observed || "nessuna"}.`;
    } catch (error) {
        status.textContent = String(error);
        stop.disabled = false; // closePlayer permits retrying a failed cleanup.
    } finally {
        player = undefined;
        highlight(new Set());
        run.disabled = false;
        stopping = false;
    }
}

function tick() {
    try {
        const context = player.context, stamp = context.getOutputTimestamp?.();
        const output = stamp?.performanceTime > 0 ? stamp.contextTime :
            context.currentTime - (context.baseLatency || 0) - (context.outputLatency || 0);
        const seconds = Math.max(0, output - startedAt);
        time.textContent = `${seconds.toFixed(2)} s`;
        highlight(activeLines(trace, path, seconds));
        const state = player.status;
        if (state) {
            void finish(state === 1 ? "OK: riproduzione terminata." : "Errore durante la riproduzione.");
            return;
        }
        animation = requestAnimationFrame(tick);
    } catch (error) { void finish(String(error)); }
}

stop.onclick = () => { void finish("Arrestato."); };
run.onclick = async () => {
    run.disabled = true;
    stop.disabled = true;
    status.textContent = "Preparazione della partitura e raccolta delle posizioni…";
    marked = new Set();
    reportSummary = "";
    const source = code.value;
    rows = source.split("\n").map((text, i) => {
        const row = document.createElement("span");
        row.textContent = `${String(i + 1).padStart(3)}  ${text}\n`;
        return row;
    });
    playing.replaceChildren(...rows);
    try {
        await closePlayer(host);
        const score = await traceScore(host, path, source);
        player = await preparePlayer(host, score, 48000);
        trace = readTrace(host);
        const missing = trace.events.filter(event => event[2].every(id => !trace.locations[id].length)).length;
        const ambiguous = trace.events.filter(event => event[2].length > 1).length;
        reportSummary = `${trace.events.length} eventi, ${trace.captures} catture dello stack, ` +
            `${trace["pushed-events"]} origini raccolte durante la costruzione delle liste, ` +
            `${trace["fallback-events"]} eventi senza provenienza conservata, ${missing} senza posizione, ` +
            `${ambiguous} con più origini possibili.`;
        const gain = player.context.createGain();
        gain.gain.value = 0.1;
        player.node.disconnect();
        player.node.connect(gain).connect(player.context.destination);
        startedAt = player.context.currentTime;
        await player.start();
        status.textContent = reportSummary;
        stop.disabled = false;
        animation = requestAnimationFrame(tick);
    } catch (error) { await finish(String(error)); }
};

try {
    if (!crossOriginIsolated) throw Error("Avviare node test/server.mjs: servono COOP/COEP.");
    host = await createPlayerHost();
    const files = [path, "lib/music.janet", "lib/pattern.janet", "lib/trace.janet"];
    for (const [plugin, binary] of [["synth_mono", "bw_example_synth_mono"], ["shape", "shape"],
        ["echo", "echo"], ["drums", "drums"]]) {
        const bundle = `plugins/${plugin}/build/plugin.perone`;
        files.push(`${bundle}/product.json`, `${bundle}/wasm32/${binary}.wasm`);
    }
    for (const file of files) {
        const response = await fetch("../" + file);
        if (!response.ok) throw Error(`Cannot load ${file}; build the Perone wasm32 plugins separately.`);
        const bytes = new Uint8Array(await response.arrayBuffer());
        await addFile(host, file, bytes);
        if (file === path) code.value = new TextDecoder().decode(bytes);
    }
    run.disabled = code.disabled = false;
    status.textContent = "Pronto: Polpo originale, volume di ascolto ridotto. Le modifiche si applicano alla prossima esecuzione.";
} catch (error) { status.textContent = String(error); }
