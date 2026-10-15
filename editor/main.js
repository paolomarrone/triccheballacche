import {timeline} from "./timeline.js";
import {plugins} from "./plugins.js";
import {settings} from "./settings.js";
import {files} from "./files.js";
import {codeEditor} from "./code.js";
import {workspace} from "./workspace.js";

const byId = id => document.getElementById(id);
const path = byId("path"), views = byId("views"), errors = byId("errors");
const code = codeEditor(byId("code"), {change: update, select: cursor, keydown: shortcut});
let backend, controls, ready = false, busy = false, playing = false, prepared = false, saved = "", savedPath = "", queue = Promise.resolve();
let live = false, audio = false, pending;
let revision = 0, frames = [], partial = false, tracedSource, tracedPath, seconds = 0;
const layout = workspace(() => controls?.show(false));

function tracking() {
    return revision && code.value === tracedSource && path.value === tracedPath;
}

function needsEvaluation() {
    return !prepared || code.value !== (pending?.source ?? tracedSource) || path.value !== (pending?.path ?? tracedPath);
}

function dirty() {
    return code.value !== saved;
}

function text(id, value) {
    const node = byId(id);
    if (node.textContent !== value) node.textContent = value;
}

function cursor() {
    text("position", code.position());
}

function update() {
    preferences.status(ready && !busy, {audio, prepared});
    controls?.status(prepared, busy);
    projection.status(prepared, busy);
    for (const id of ["open", "save", "prepare", "play", "views"]) byId(id).disabled = !ready || busy;
    byId("stop").disabled = !ready || busy || !playing;
    byId("audio").disabled = !ready || busy || !prepared;
    byId("audio").checked = audio;
    byId("play").title = `${needsEvaluation() ? "Evaluate and play" : "Play"} · Ctrl+Space · Ctrl+Enter to prepare`;
    byId("rewind").disabled = byId("time").disabled = !ready || busy || !prepared;
    path.disabled = !ready || busy;
    code.readOnly(!ready || busy);
    text("modified", dirty() ? "●" : "");
    text("state", !ready ? "Connecting…" : busy ? "Please wait…" : !playing ? "Stopped" :
        pending ? "Playing · revision queued" : tracking() ? `Playing${partial ? " · partial origins" : ""}` : "Playing · tracking paused: press Play to apply changes");
    cursor();
    document.title = `${dirty() ? "* " : ""}${savedPath ? savedPath + " · " : ""}triccheballacche`;
    code.mark(playing && tracking() ? frames.filter(frame => frame[0] === tracedPath).map(frame => frame[1]) : []);
}

function showError(error) {
    errors.textContent = error?.message || String(error || "");
    errors.hidden = !errors.textContent;
}

// Serialize editor operations across either backend, including viewport requests and polling.
function request(op, ...args) {
    const result = queue.then(async () => {
        const response = await backend.command(op, ...args);
        // Any command may observe a live revision. A successful Run replaces the
        // whole session; its score must not inherit the old session's queued source.
        if (pending && (!["run", "prepare"].includes(op) || response.error) && Number.isInteger(response.revision) && response.revision !== revision) {
            const result = response.score ? response : await backend.command("score");
            if (result.error) throw Error(result.error);
            revision = result.score.revision;
            projection.revise(result.score);
            controls.revise(result.score);
            preferences.score(result.score);
            tracedSource = pending.source;
            tracedPath = pending.path;
            pending = undefined;
        }
        if (response.queued === false) pending = undefined;
        if (typeof response.playing === "boolean") playing = response.playing;
        if (typeof response.audio === "boolean") audio = response.audio;
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
    busy = true;
    showError("");
    update();
    try {
        // Prior commands may invalidate the preparation or finish a live revision.
        await queue;
        if (op === "play" && needsEvaluation()) op = "run";
        const evaluating = op === "run" || op === "prepare";
        const updating = evaluating && live && playing && entry === tracedPath;
        if ((opening || ["save", "run", "prepare"].includes(op)) && !entry.trim()) throw Error("Enter a score path.");
        if (["run", "prepare", "play", "seek"].includes(op)) frames = [];
        if (evaluating && !updating) controls.dispose();
        if (file?.size > 8 * 1024 * 1024) throw Error("Score too large (at most 8 MiB)");
        const source = file ? await file.text() : ["save", "run", "prepare"].includes(op) ? code.value : "";
        const result = await request(updating ? "update" : op, entry, source);
        if (evaluating && result.queued) {
            pending = {source, path: result.path};
        } else if (evaluating) {
            pending = undefined;
            revision = result.score.revision;
            projection.score(result.score);
            tracedSource = source;
            tracedPath = result.path;
            controls.score(result.score, result.nativeAvailable);
            preferences.score(result.score);
        }
        if (opening) { layout.view("code"); code.load(result.text); }
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

const preferences = settings(request, entry => action("open", entry), showError);
const pickFile = files(request, entry => action("open", entry),
    (file, directory) => action("import", `${directory.replace(/\/$/, "")}/${file.name}`, file));

const projection = timeline(request, origins => {
    if (!tracking()) return;
    const frame = origins.find(([file]) => file === tracedPath);
    if (!frame) return;
    layout.view("code");
    code.reveal(frame[1]);
    update();
}, {
    track(index) { controls?.track(index); },
    node(id, path) { controls?.node(id, path); }
}, time => action("seek", time), showError);

for (const op of ["save", "prepare", "play", "stop"]) byId(op).addEventListener("click", () => action(op));
byId("audio").onchange = () => action("audio", Number(byId("audio").checked));
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
window.addEventListener("pagehide", () => { code.destroy(); controls?.dispose(); backend?.close?.().catch(showError); });
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
function shortcut(event) {
    if (event.defaultPrevented || document.querySelector("dialog:modal, :popover-open")) return;
    let op;
    if ((event.ctrlKey || event.metaKey) && event.key === "Enter") op = "prepare";
    if ((event.ctrlKey || event.metaKey) && event.code === "Space") op = "play";
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") op = "save";
    if (event.key === "Escape" && playing) op = "stop";
    if (op) {
        event.preventDefault();
        action(op);
    }
}
document.addEventListener("keydown", shortcut);

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
    controls = plugins(request, adapter, showError, layout.plugins);
    byId("save").setAttribute("aria-label", backend.saveLabel);
    byId("save").title = backend.saveTitle;
    try {
        await backend.connect();
        window.onbeforeunload = beforeUnload;
        ready = true;
        const result = await request("open");
        path.value = savedPath = result.path;
        code.load(result.text);
        saved = result.text;
        await preferences.load();
    } catch (error) {
        showError(error);
    }
    update();
    poll();
}
