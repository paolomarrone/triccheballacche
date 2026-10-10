import {timeline} from "./timeline.js";
import {plugins} from "./plugins.js";
import {library} from "./library.js";
import {files} from "./files.js";
import {documents} from "./documents.js";
import {resolve} from "./project.js";

const byId = id => document.getElementById(id);
const path = byId("path"), views = byId("views"), errors = byId("errors");
let code = byId("code");
const numbers = byId("numbers"), marks = byId("marks");
let backend, controls, ready = false, busy = false, playing = false, prepared = false, queue = Promise.resolve();
let live = false, pending;
let revision = 0, frames = [], partial = false, applied, seconds = 0, displayedSource, lineCount = 1, painted = "";

function tracking() {
    return revision && applied?.project === workspace.project && applied.sources[workspace.project.active] === code.value;
}

function needsEvaluation() {
    return !prepared || !workspace.project?.matches(pending ?? applied);
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
    const lines = playing && tracking() ? new Set(frames.filter(frame => resolve(frame[0], workspace.project.cwd) === workspace.project.active).map(frame => frame[1])) : new Set();
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
    return workspace.dirty();
}

function update() {
    code = workspace.code;
    workspace.status(ready && !busy);
    browser.status(ready && !busy);
    controls?.status(prepared, busy);
    projection.status(prepared, busy);
    for (const id of ["open", "save", "play", "views"]) byId(id).disabled = !ready || busy;
    byId("play").disabled ||= !workspace.project;
    byId("save").disabled ||= !workspace.project;
    byId("stop").disabled = !ready || busy || !playing;
    byId("play").title = `${needsEvaluation() ? "Evaluate and play" : "Play"} · Ctrl+Space · Ctrl+Enter to reevaluate`;
    byId("rewind").disabled = byId("time").disabled = !ready || busy || !prepared;
    path.disabled = !ready || busy;
    code.disabled = !ready || !workspace.project;
    code.readOnly = busy;
    byId("modified").textContent = workspace.project?.current.text !== workspace.project?.current.saved ? "●" : "";
    byId("state").textContent = !ready ? "Connecting…" : busy ? "Please wait…" : !playing ? "Stopped" :
        pending ? "Playing · revision queued" : tracking() ? `Playing${partial ? " · partial origins" : ""}` : "Playing · tracking paused: press Play to apply changes";
    const before = code.value.slice(0, code.selectionStart).split("\n");
    byId("position").textContent = `${before.length}:${before.at(-1).length + 1}`;
    document.title = `${dirty() ? "* " : ""}${workspace.project ? workspace.project.current.path + " · " : ""}triccheballacche`;
    paint();
}

function showError(error) {
    for (const element of [errors, byId("document-error")]) { element.textContent = ""; element.hidden = true; }
    const target = byId("document-dialog").open ? byId("document-error") : errors;
    target.textContent = error?.message || String(error || "");
    target.hidden = !target.textContent;
}

// Serialize editor operations across either backend, including viewport requests and polling.
function request(op, ...args) {
    const result = queue.then(async () => {
        const response = await backend.command(op, ...args);
        // Any command may observe a live revision. A successful Run replaces the
        // whole session; its score must not inherit the old session's queued source.
        if (pending && (op !== "run" || response.error) && Number.isInteger(response.revision) && response.revision !== revision) {
            const result = response.score ? response : await backend.command("score");
            if (result.error) throw Error(result.error);
            revision = result.score.revision;
            projection.revise(result.score);
            controls.revise(result.score);
            browser.score(result.score);
            applied = pending;
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
    if (op === "play" && needsEvaluation()) op = "run";
    const opening = op === "open" || op === "import" || op === "project-new";
    if (opening && dirty() && !confirm("Open another project and discard unsaved changes?")) return;
    busy = true;
    if (["run", "play", "seek"].includes(op)) frames = [];
    showError("");
    update();
    try {
        if (["open", "save", "import", "document-import", "document-open", "document-new", "document-remove", "project-new", "project-save"].includes(op)) {
            if (file?.size > 8 * 1024 * 1024) throw Error("Document too large (at most 8 MiB)");
            await workspace.command(op, entry, file ? await file.text() : undefined);
        } else if (op === "run") {
            if (!workspace.project) throw Error("Open or create a project first");
            const snapshot = workspace.project.snapshot();
            const updating = live && playing && applied?.project === snapshot.project && applied.entry === snapshot.entry;
            if (!updating) controls.dispose();
            const result = await request(updating ? "update" : "run", snapshot.path, snapshot.sources[snapshot.entry], JSON.stringify(snapshot.sources));
            if (result.queued) pending = snapshot;
            else {
                pending = undefined;
                applied = snapshot;
                revision = result.score.revision;
                projection.score(result.score);
                controls.score(result.score, result.nativeAvailable);
                browser.score(result.score);
            }
        } else await request(op, entry);
    } catch (error) {
        showError(error);
    } finally {
        busy = false;
        update();
    }
}

const workspace = documents(request, action, update);

const browser = library(request, entry => action("open", entry), showError);
let addingDocument = false;
const pickFile = files(request, entry => action(addingDocument ? "document-open" : "open", entry),
    (file, directory) => action(addingDocument ? "document-import" : "import", `${directory.replace(/\/$/, "")}/${file.name}`, file));

const projection = timeline(request, origins => {
    if (!tracking()) return;
    const frame = origins.find(([file]) => resolve(file, workspace.project.cwd) === workspace.project.active);
    if (!frame) return;
    const rows = code.value.split("\n"), line = Math.max(0, frame[1] - 1);
    const offset = rows.slice(0, line).reduce((length, row) => length + row.length + 1, 0);
    code.focus(); code.setSelectionRange(offset, offset + (rows[line]?.length || 0));
    code.scrollTop = Math.max(0, line * parseFloat(getComputedStyle(code).lineHeight) - code.clientHeight / 2);
    update();
}, {
    track(index) { views.checked = true; controls?.track(index); },
    node(id, path) { views.checked = true; controls?.node(id, path); }
}, time => action("seek", time), showError);
new ResizeObserver(paint).observe(byId("sheet"));

for (const op of ["save", "play", "stop"]) byId(op).addEventListener("click", () => action(op));
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
byId("open").onclick = () => { addingDocument = false; pickFile(path.value, backend.canUpload); };
byId("open-document").onclick = () => {
    addingDocument = true;
    byId("project-menu").hidePopover();
    pickFile(path.value, backend.canUpload);
};
for (const button of document.querySelectorAll("[data-close]")) button.onclick = () => button.closest("dialog").close();
views.addEventListener("change", () => controls?.show(views.checked));
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
    if (event.defaultPrevented || document.querySelector("dialog[open], :popover-open")) return;
    let op;
    if ((event.ctrlKey || event.metaKey) && event.key === "Enter") op = "run";
    if ((event.ctrlKey || event.metaKey) && event.code === "Space") op = "play";
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") op = event.shiftKey ? "project-save" : "save";
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
        await workspace.connect();
        window.onbeforeunload = beforeUnload;
        ready = true;
        const result = await request("open");
        await workspace.start(result);
        await browser.load();
    } catch (error) {
        showError(error);
    }
    update();
    poll();
}
