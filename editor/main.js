import {timeline} from "./timeline.js";
import {plugins} from "./plugins.js";

const byId = id => document.getElementById(id);
const path = byId("path"), code = byId("code"), views = byId("views"), errors = byId("errors");
const numbers = byId("numbers"), marks = byId("marks");
let backend, controls, ready = false, busy = false, playing = false, saved = "", savedPath = "", queue = Promise.resolve();
let revision = 0, frames = [], partial = false, tracedSource, tracedPath, seconds = 0, displayedSource, lineCount = 1, painted = "";

function tracking() {
    return revision && code.value === tracedSource && path.value === tracedPath;
}

function paint() {
    if (code.value !== displayedSource) {
        displayedSource = code.value;
        lineCount = displayedSource.split("\n").length;
    }
    // Draw only visible rows. The textarea retains editing, selection and native scrolling.
    const style = getComputedStyle(code), height = parseFloat(style.lineHeight);
    const first = Math.max(0, Math.floor((code.scrollTop - parseFloat(style.paddingTop)) / height));
    const last = Math.min(lineCount, Math.ceil((code.scrollTop + code.clientHeight) / height));
    const lines = playing && tracking() ? new Set(frames.filter(frame => frame[0] === tracedPath).map(frame => frame[1])) : new Set();
    const key = `${first}:${last}:${[...lines].join(",")}`;
    if (key !== painted) {
        const labels = document.createDocumentFragment(), highlights = document.createDocumentFragment();
        for (let i = first; i < last; ++i) {
            const number = document.createElement("span");
            number.textContent = number.dataset.line = i + 1;
            number.style.top = `calc(${i} * var(--row))`;
            if (lines.has(i + 1)) {
                number.className = "active";
                const mark = document.createElement("div");
                mark.dataset.line = i + 1;
                mark.style.top = number.style.top;
                highlights.append(mark);
            }
            labels.append(number);
        }
        numbers.replaceChildren(labels);
        marks.replaceChildren(highlights);
        painted = key;
    }
    numbers.style.transform = marks.style.transform = `translateY(${-code.scrollTop}px)`;
}

function dirty() {
    return code.value !== saved;
}

function update() {
    controls?.status(playing, busy);
    for (const id of ["open", "save", "run", "views"]) byId(id).disabled = !ready || busy;
    byId("stop").disabled = !ready || busy || !playing;
    path.disabled = !ready || busy;
    code.disabled = !ready;
    code.readOnly = busy;
    byId("modified").textContent = dirty() ? "●" : "";
    byId("state").textContent = !ready ? "Collegamento…" : busy ? "Attendere…" : !playing ? "Fermo" :
        tracking() ? `In ascolto${partial ? " · origini parziali" : ""}` : "In ascolto · tracking sospeso: riesegui le modifiche";
    const before = code.value.slice(0, code.selectionStart).split("\n");
    byId("position").textContent = `${before.length}:${before.at(-1).length + 1}`;
    document.title = `${dirty() ? "* " : ""}${savedPath || "triccheballacche"}`;
    paint();
}

function showError(error) {
    errors.textContent = error?.message || String(error || "");
    errors.hidden = !errors.textContent;
}

// Serialize editor operations across either backend, including viewport requests and polling.
function request(op, ...args) {
    const result = queue.then(async () => {
        const response = await backend.command(op, ...args);
        if (typeof response.playing === "boolean") playing = response.playing;
        if (Number.isFinite(response.time)) {
            seconds = response.time;
            byId("time").textContent = `${seconds.toFixed(2)} s`;
        }
        if (op === "status") {
            frames = response.revision === revision ? response.frames || [] : [];
            partial = response.truncated;
        }
        projection.position(seconds, playing);
        update();
        if (response.error) throw Error(response.error);
        return response;
    });
    queue = result.catch(() => {});
    return result;
}

async function action(op) {
    if (!ready || busy) return;
    if (op === "open" && dirty() && !confirm("Aprire un altro file e scartare le modifiche non salvate?")) return;
    if (["open", "save", "run"].includes(op) && !path.value.trim()) {
        showError("Indica il percorso della partitura.");
        return;
    }
    busy = true;
    if (op === "run") frames = [];
    showError("");
    update();
    try {
        if (op === "run" || op === "stop") controls.dispose();
        const result = await request(op, path.value, ["save", "run"].includes(op) ? code.value : "", views.checked);
        if (op === "run") {
            revision = result.score.revision;
            projection.score(result.score);
            tracedSource = code.value;
            tracedPath = result.path;
            await controls.score(result.score);
        }
        if (op === "open") code.value = result.text;
        if (op === "open" || op === "save") {
            path.value = savedPath = result.path;
            saved = code.value;
        }
    } catch (error) {
        showError(error);
    } finally {
        busy = false;
        update();
    }
}

const projection = timeline(request, origins => {
    if (!tracking()) return;
    const frame = origins.find(([file]) => file === tracedPath);
    if (!frame) return;
    const rows = code.value.split("\n"), line = Math.max(0, frame[1] - 1);
    const offset = rows.slice(0, line).reduce((length, row) => length + row.length + 1, 0);
    code.focus(); code.setSelectionRange(offset, offset + (rows[line]?.length || 0));
    code.scrollTop = Math.max(0, line * parseFloat(getComputedStyle(code).lineHeight) - code.clientHeight / 2);
    update();
}, showError);
new ResizeObserver(paint).observe(code);

for (const op of ["open", "save", "run", "stop"]) byId(op).addEventListener("click", () => action(op));
views.addEventListener("change", () => controls?.show(views.checked).catch(showError));
for (const event of ["input", "click", "keyup", "select"]) code.addEventListener(event, update);
code.addEventListener("scroll", paint);
window.addEventListener("resize", paint);
window.addEventListener("pagehide", () => { controls?.dispose(); backend?.close?.().catch(showError); });
path.addEventListener("input", update);
document.addEventListener("keydown", event => {
    let op;
    if ((event.ctrlKey || event.metaKey) && event.key === "Enter") op = "run";
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") op = "save";
    if (event.key === "Escape" && playing) op = "stop";
    if (op) {
        event.preventDefault();
        action(op);
    }
});

// Cancelling a close must preserve the unsaved buffer and its backend.
function beforeUnload(event) {
    if (dirty()) {
        event.preventDefault();
        event.returnValue = "";
    }
}

async function poll() {
    if (ready && !busy) {
        try { await request("status", "", "", false); await controls.poll(); }
        catch (error) { showError(error); }
    }
    setTimeout(poll, 50);
}

export async function startEditor(adapter) {
    backend = adapter;
    controls = plugins(request, adapter, showError);
    byId("plugin-views").hidden = !backend.views;
    byId("save").textContent = backend.saveLabel;
    byId("save").title = backend.saveTitle;
    try {
        await backend.connect();
        window.onbeforeunload = beforeUnload;
        ready = true;
        const result = await request("open", "", "", false);
        path.value = savedPath = result.path;
        code.value = saved = result.text;
    } catch (error) {
        showError(error);
    }
    update();
    poll();
}
