import {timeline} from "./timeline.js";
import {plugins} from "./plugins.js";
import {library} from "./library.js";
import {files} from "./files.js";

const byId = id => document.getElementById(id);
const path = byId("path"), code = byId("code"), views = byId("views"), errors = byId("errors");
const numbers = byId("numbers"), marks = byId("marks");
let backend, controls, ready = false, busy = false, playing = false, prepared = false, saved = "", savedPath = "", queue = Promise.resolve();
let live = false, pending;
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
    browser.status(ready && !busy);
    controls?.status(prepared, busy);
    projection.status(prepared, busy);
    for (const id of ["open", "save", "run", "views"]) byId(id).disabled = !ready || busy;
    byId("stop").disabled = !ready || busy || !playing;
    byId("play").disabled = !ready || busy || !prepared;
    byId("rewind").disabled = byId("time").disabled = !ready || busy || !prepared;
    path.disabled = !ready || busy;
    code.disabled = !ready;
    code.readOnly = busy;
    byId("modified").textContent = dirty() ? "●" : "";
    byId("state").textContent = !ready ? "Connecting…" : busy ? "Please wait…" : !playing ? "Stopped" :
        pending ? "Playing · revision queued" : tracking() ? `Playing${partial ? " · partial origins" : ""}` : "Playing · tracking paused: rerun your changes";
    const before = code.value.slice(0, code.selectionStart).split("\n");
    byId("position").textContent = `${before.length}:${before.at(-1).length + 1}`;
    document.title = `${dirty() ? "* " : ""}${savedPath ? savedPath + " · " : ""}triccheballacche`;
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
        // Any command may observe the audio boundary, including Stop or another Run.
        // Adopt that revision before its pending source can be replaced by a new submission.
        if (pending && Number.isInteger(response.revision) && response.revision !== revision) {
            const result = response.score ? response : await backend.command("score");
            if (result.error) throw Error(result.error);
            revision = result.score.revision;
            projection.revise(result.score);
            controls.revise(result.score);
            browser.score(result.score);
            tracedSource = pending.source;
            tracedPath = pending.path;
            pending = undefined;
        }
        if (response.queued === false) pending = undefined;
        if (typeof response.playing === "boolean") playing = response.playing;
        if (typeof response.prepared === "boolean") prepared = response.prepared;
        if (typeof response.live === "boolean") live = response.live;
        if (Number.isFinite(response.time)) {
            seconds = response.time;
            if (document.activeElement !== byId("time")) byId("time").value = seconds.toFixed(2);
        }
        if (op === "status") {
            if (response.nativeOpen) controls?.windows(response.nativeOpen);
            frames = response.revision === revision ? response.frames || [] : [];
            partial = response.truncated;
        }
        projection.position(seconds, playing, op === "seek" && !response.error);
        update();
        if (response.error) throw Error(response.error);
        return response;
    });
    queue = result.catch(() => {});
    return result;
}

async function action(op, entry = path.value, file) {
    if (!ready || busy) return;
    const opening = op === "open" || op === "import";
    if (opening && dirty() && !confirm("Open another file and discard unsaved changes?")) return;
    if ((opening || ["save", "run"].includes(op)) && !entry.trim()) {
        showError("Enter a score path.");
        return;
    }
    busy = true;
    if (["run", "play", "seek"].includes(op)) frames = [];
    showError("");
    update();
    try {
        if (op === "run" && !(live && playing)) controls.dispose();
        if (file?.size > 8 * 1024 * 1024) throw Error("Score too large (at most 8 MiB)");
        const source = file ? await file.text() : ["save", "run"].includes(op) ? code.value : "";
        const result = await request(op, entry, source);
        if (op === "run" && result.queued) {
            pending = {source, path: result.path};
        } else if (op === "run") {
            pending = undefined;
            revision = result.score.revision;
            projection.score(result.score);
            tracedSource = source;
            tracedPath = result.path;
            controls.score(result.score, result.nativeAvailable);
            browser.score(result.score);
        }
        if (opening) code.value = result.text;
        if (opening || op === "save") {
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

const browser = library(request, entry => action("open", entry), showError);
const pickFile = files(request, entry => action("open", entry),
    (file, directory) => action("import", `${directory.replace(/\/$/, "")}/${file.name}`, file));

const projection = timeline(request, origins => {
    if (!tracking()) return;
    const frame = origins.find(([file]) => file === tracedPath);
    if (!frame) return;
    const rows = code.value.split("\n"), line = Math.max(0, frame[1] - 1);
    const offset = rows.slice(0, line).reduce((length, row) => length + row.length + 1, 0);
    code.focus(); code.setSelectionRange(offset, offset + (rows[line]?.length || 0));
    code.scrollTop = Math.max(0, line * parseFloat(getComputedStyle(code).lineHeight) - code.clientHeight / 2);
    update();
}, index => { views.checked = true; controls?.track(index); }, time => action("seek", time), showError);
new ResizeObserver(paint).observe(code);

for (const op of ["save", "run", "play", "stop"]) byId(op).addEventListener("click", () => action(op));
byId("rewind").onclick = () => action("seek", 0);
byId("time").onkeydown = event => {
    if (event.key === "Enter" && !event.ctrlKey && !event.metaKey) {
        event.preventDefault();
        const time = byId("time").valueAsNumber;
        byId("time").blur();
        action("seek", time);
    } else if (event.key === "Escape") {
        event.preventDefault();
        byId("time").blur();
    }
};
byId("time").onblur = () => { byId("time").value = seconds.toFixed(2); };
byId("open").onclick = () => pickFile(path.value, backend.canUpload);
for (const button of document.querySelectorAll("[data-close]")) button.onclick = () => button.closest("dialog").close();
views.addEventListener("change", () => controls?.show(views.checked));
for (const event of ["input", "click", "keyup", "select"]) code.addEventListener(event, update);
code.addEventListener("scroll", paint);
window.addEventListener("resize", paint);
window.addEventListener("pagehide", () => { controls?.dispose(); backend?.close?.().catch(showError); });
window.addEventListener("close-request", () => {
    if (dirty() && !confirm("Close and discard unsaved changes?")) return;
    window.onbeforeunload = null;
    request("close").catch(error => { window.onbeforeunload = beforeUnload; showError(error); });
});
path.addEventListener("input", update);
path.addEventListener("keydown", event => {
    if (event.key === "Enter" && !event.ctrlKey && !event.metaKey) {
        event.preventDefault();
        action("open");
    }
});
document.addEventListener("keydown", event => {
    if (event.defaultPrevented || document.querySelector("dialog[open]")) return;
    let op;
    if ((event.ctrlKey || event.metaKey) && event.key === "Enter") op = "run";
    if ((event.ctrlKey || event.metaKey) && event.code === "Space" && prepared) op = "play";
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
        try {
            await request("status");
            await controls.poll();
        }
        catch (error) { showError(error); }
    }
    setTimeout(poll, 50);
}

export async function startEditor(adapter) {
    backend = adapter;
    controls = plugins(request, adapter, showError);
    byId("save").setAttribute("aria-label", backend.saveLabel);
    byId("save").title = backend.saveTitle;
    try {
        await backend.connect();
        window.onbeforeunload = beforeUnload;
        ready = true;
        const result = await request("open");
        path.value = savedPath = result.path;
        code.value = saved = result.text;
        await browser.load();
    } catch (error) {
        showError(error);
    }
    update();
    poll();
}
