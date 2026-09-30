import {create as generic} from "./perone-ui.js";
import {pluginControls} from "./plugin-controls.js";
import {trackNodes} from "./graph.js";

// Views belong to the prepared project and survive transport stop/restart.
export function plugins(request, adapter, fail) {
    const panel = document.getElementById("plugins"), list = document.getElementById("plugin-list");
    const entries = new Map();
    let score, availableProject = false, busy = false, visible = false, track = 0, node;
    let chain = [];
    let available = new Set(), native = new Set();

    function detach(entry) {
        const old = entry.token;
        if (!old) return;
        entry.token = undefined; // Ignore callbacks from free() and factories that finish after disposal.
        old.controls.close();
        try { old.ui?.free(); }
        catch (error) { fail(error); }
        entry.body.replaceChildren();
        if (availableProject) request("watch", old.revision, entry.id, "off").catch(() => {});
    }

    function dispose() {
        for (const entry of entries.values()) detach(entry);
    }

    function show(value) {
        visible = value; panel.hidden = !visible;
        if (!visible) dispose();
        else for (const entry of entries.values()) if (!entry.token) mount(entry).catch(fail);
    }

    function failed(entry, token, error) {
        if (entry.token !== token) return;
        detach(entry);
        fail(error);
    }

    function buttons(entry) {
        for (const button of entry.details.querySelectorAll("summary button")) button.disabled = !availableProject || busy || entry.pending;
        entry.window?.setAttribute("aria-pressed", native.has(entry.id));
        if (entry.window) entry.window.title = native.has(entry.id) ? "Close native UI" : "Open native UI";
        entry.parameters?.setAttribute("aria-pressed", entry.generic);
        if (entry.parameters) entry.parameters.title = entry.generic ? "Show plugin UI" : "Show parameters";
    }

    async function windowView(entry) {
        if (!availableProject || busy || entry.pending) return;
        entry.pending = true;
        const opening = !native.has(entry.id), revision = score.controlRevision ?? score.revision;
        entry.details.open = false;
        detach(entry);
        buttons(entry);
        try {
            await request("watch", revision, entry.id, opening ? "native" : "off");
            if ((score.controlRevision ?? score.revision) === revision) opening ? native.add(entry.id) : native.delete(entry.id);
        } catch (error) { fail(error); }
        finally { entry.pending = false; buttons(entry); }
    }

    function render() {
        panel.hidden = !visible;
        if (!score) return;
        for (const [id, entry] of entries) if (!chain.includes(id)) {
            detach(entry); entry.details.remove(); entries.delete(id);
        }
        for (const id of chain) {
            if (entries.has(id)) continue;
            const details = document.createElement("details"), summary = document.createElement("summary");
            const name = document.createElement("span"), body = document.createElement("div");
            const entry = {id, details, body, generic: false};
            entries.set(id, entry);
            details.className = "plugin"; details.dataset.node = id;
            name.className = "plugin-name"; name.textContent = name.title = score.nodes[id].name;
            body.className = "plugin-body";
            summary.onclick = event => { if (entry.pending || busy) event.preventDefault(); };
            summary.append(name); details.append(summary, body);
            if (!score.nodes[id].product) {
                body.textContent = `Inputs: ${score.nodes[id].inputs.map(input => score.nodes[input].label || score.nodes[input].name).join(", ") || "none"}`;
                continue;
            }
            const button = (text, title, action) => {
                const control = document.createElement("button");
                control.textContent = text; control.setAttribute("aria-label", title);
                control.onclick = event => { event.preventDefault(); event.stopPropagation(); action(); };
                summary.append(control);
                return control;
            };
            if (score.nodes[id].product.ui?.web) entry.parameters = button("≡", "Toggle parameter controls", () => {
                entry.generic = !entry.generic;
                detach(entry); entry.details.open = true;
                mount(entry).catch(fail); buttons(entry);
            });
            if (available.has(id)) entry.window = button("↗", "Toggle native UI", () => windowView(entry));
            details.ontoggle = () => {
                if (!details.open) detach(entry);
                else if (!entry.token) mount(entry).catch(fail);
            };
            buttons(entry);
        }
        chain.forEach((id, index) => {
            const entry = entries.get(id);
            if (list.children[index] !== entry.details) list.insertBefore(entry.details, list.children[index] || null);
            entry.details.open = id === (node ?? chain[0]) && (node !== undefined || !native.has(id));
            if (!entry.details.open) detach(entry);
            else if (!entry.token) mount(entry).catch(fail);
        });
    }

    function trackChain(index) {
        const track = score.tracks[index];
        return track ? trackNodes(score, track).filter(id => id !== track[1]) : [];
    }

    async function mount(entry) {
        if (entries.get(entry.id) !== entry || !score.nodes[entry.id].product || !availableProject || busy || !visible || !entry.details.open || entry.pending) return;
        detach(entry);
        fail("");
        const id = entry.id, node = score.nodes[id];
        const token = entry.token = {revision: score.controlRevision ?? score.revision};
        token.controls = pluginControls(node.product, (op, ...args) => request(op, token.revision, id, ...args),
            error => failed(entry, token, error));
        try {
            if (native.has(id)) {
                await request("watch", token.revision, id, "off");
                if (entry.token !== token) return;
                native.delete(id); buttons(entry);
            }
            const host = token.host = document.createElement("div"), shadow = host.attachShadow({mode: "open"});
            // Keep layout available to UI factories without exposing their default values.
            host.style.visibility = "hidden"; host.inert = true;
            const style = document.createElement("style");
            style.textContent = `:host { display: block; font: inherit; -webkit-user-select: none; user-select: none; }
                input, textarea, [contenteditable=true] { -webkit-user-select: text; user-select: text; }
                button, input, select { font: inherit; }`;
            const element = document.createElement("div");
            shadow.append(style, element);
            entry.body.append(host);
            const create = !entry.generic && node.product.ui?.web ?
                (await import(adapter.uiUrl(node, token.revision, id))).create : generic;
            if (entry.token !== token) return;
            const ui = await create(element, token.controls.callbacks);
            if (!ui || typeof ui.free !== "function") throw Error("Perone UI must return free()");
            if (entry.token !== token) { ui.free(); return; }
            token.ui = ui;
            // No DSP stream until the factory is ready to receive it. Creation-time gestures stay queued.
            await request("watch", token.revision, id, "web");
            if (entry.token !== token) return;
            token.controls.attach(ui);
            // Reveal only after current values, including host overrides, have arrived.
            await pollEntry(entry);
        } catch (error) { failed(entry, token, error); }
    }

    async function pollEntry(entry) {
        const token = entry.token;
        if (token && await token.controls.poll() && entry.token === token && token.host.inert) {
            token.host.style.visibility = ""; token.host.inert = false;
        }
    }

    return {
        score(next, nativeAvailable = []) {
            dispose(); entries.clear(); list.replaceChildren();
            score = next;
            node = undefined;
            available = new Set(nativeAvailable); native.clear();
            track = Math.max(0, Math.min(track, next.tracks.length - 1));
            chain = trackChain(track);
            render();
        },
        revise(next) {
            score = next;
        },
        track(index) {
            if (index === track && node === undefined) show(true);
            else { track = index; node = undefined; chain = trackChain(index); visible = true; render(); }
        },
        node(id, path) {
            if (!score?.nodes[id]) return;
            node = id; chain = path;
            render();
            show(true);
            entries.get(id).details.scrollIntoView({block: "nearest", inline: "nearest"});
        },
        show,
        windows(ids) {
            native = new Set(ids);
            for (const entry of entries.values()) buttons(entry);
        },
        status(value, pending) {
            if (availableProject === value && busy === pending) return;
            const resumed = busy && !pending;
            availableProject = value; busy = pending;
            if (!availableProject) { dispose(); native.clear(); }
            for (const entry of entries.values()) {
                buttons(entry);
                if (availableProject && resumed && !entry.token) mount(entry).catch(fail);
            }
        },
        dispose,
        async poll() { for (const entry of [...entries.values()]) await pollEntry(entry); }
    };
}
