import {activeLines} from "./trace.js";

const byId = id => document.getElementById(id);
const path = byId("path"), code = byId("code"), views = byId("views"), errors = byId("errors");
const numbers = byId("numbers"), marks = byId("marks");
let ready = false, busy = false, playing = false, saved = "", savedPath = "", queue = Promise.resolve();
let trace, tracedSource, tracedPath, seconds = 0, displayedSource, lineCount = 1, painted = "";

function tracking() {
    return trace && code.value === tracedSource && path.value === tracedPath;
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
    const lines = playing && tracking() ? activeLines(trace, tracedPath, seconds) : new Set();
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
    for (const id of ["open", "save", "run", "views"]) byId(id).disabled = !ready || busy;
    byId("stop").disabled = !ready || busy || !playing;
    path.disabled = !ready || busy;
    code.disabled = !ready;
    code.readOnly = busy;
    byId("modified").textContent = dirty() ? "●" : "";
    byId("state").textContent = !ready ? "Collegamento…" : busy ? "Attendere…" : !playing ? "Fermo" :
        tracking() ? "In ascolto" : "In ascolto · tracking sospeso: riesegui le modifiche";
    const before = code.value.slice(0, code.selectionStart).split("\n");
    byId("position").textContent = `${before.length}:${before.at(-1).length + 1}`;
    document.title = `${dirty() ? "* " : ""}${savedPath || "triccheballacche"}`;
    paint();
}

function showError(error) {
    errors.textContent = error?.message || String(error || "");
    errors.hidden = !errors.textContent;
}

// Serialize browser calls; all Janet, player and plugin-window operations run on the native main thread.
function request(op, file = "", source = "", showViews = views.checked) {
    const result = queue.then(async () => {
        const response = JSON.parse(await webui.call("command", op, file, source, showViews));
        if (typeof response.playing === "boolean") playing = response.playing;
        if (Number.isFinite(response.time)) {
            seconds = response.time;
            byId("time").textContent = `${seconds.toFixed(2)} s`;
        }
        if (!playing) trace = undefined;
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
    if (op === "run") trace = undefined;
    showError("");
    update();
    try {
        const result = await request(op, path.value, ["save", "run"].includes(op) ? code.value : "");
        if (op === "run") {
            trace = result.trace;
            tracedSource = code.value;
            tracedPath = result.path;
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

for (const op of ["open", "save", "run", "stop"]) byId(op).addEventListener("click", () => action(op));
views.addEventListener("change", () => action("views"));
for (const event of ["input", "click", "keyup", "select"]) code.addEventListener(event, update);
code.addEventListener("scroll", paint);
window.addEventListener("resize", paint);
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

// Replace WebUI 2.4's eager disconnect: cancelling a close must keep the backend alive.
window.onbeforeunload = event => {
    if (dirty()) {
        event.preventDefault();
        event.returnValue = "";
    }
};

async function poll() {
    if (ready && !busy) {
        try { await request("status"); }
        catch (error) { showError(error); }
    }
    setTimeout(poll, 50);
}

async function start() {
    try {
        // The bridge connects asynchronously; calls reject until its socket is ready.
        for (let attempt = 0; ; ++attempt) {
            try { await request("status"); break; }
            catch (error) {
                if (attempt === 30) throw error;
                await new Promise(resolve => setTimeout(resolve, 100));
            }
        }
        ready = true;
        const result = await request("open");
        path.value = savedPath = result.path;
        code.value = saved = result.text;
    } catch (error) {
        showError(error);
    }
    update();
    poll();
}

start();
