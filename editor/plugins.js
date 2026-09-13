import {create as generic} from "./perone-ui.js";

// Each expanded section owns one view; native windows live until explicitly closed or playback stops.
export function plugins(request, adapter, fail) {
    const panel = document.getElementById("plugins"), list = document.getElementById("plugin-list");
    const entries = new Map();
    let score, running = false, busy = false, visible = false, track = 0;
    let available = new Set(), native = new Set();

    function detach(entry) {
        const old = entry.token;
        entry.token = undefined; // Ignore callbacks from free() and factories that finish after disposal.
        try { old?.ui?.free(); }
        catch (error) { fail(error); }
        entry.body.replaceChildren();
        if (old && running) request("watch", old.revision, entry.id, "off").catch(() => {});
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
        for (const button of entry.details.querySelectorAll("summary button")) button.disabled = !running || busy || entry.pending;
        entry.window?.setAttribute("aria-pressed", native.has(entry.id));
        if (entry.window) entry.window.title = native.has(entry.id) ? "Close native UI" : "Open native UI";
        entry.parameters?.setAttribute("aria-pressed", entry.generic);
        if (entry.parameters) entry.parameters.title = entry.generic ? "Show plugin UI" : "Show parameters";
    }

    async function windowView(entry) {
        if (!running || busy || entry.pending) return;
        entry.pending = true;
        const opening = !native.has(entry.id), revision = score.revision;
        entry.details.open = false;
        detach(entry);
        buttons(entry);
        try {
            await request("watch", revision, entry.id, opening ? "native" : "off");
            if (score.revision === revision) opening ? native.add(entry.id) : native.delete(entry.id);
        } catch (error) { fail(error); }
        finally { entry.pending = false; buttons(entry); }
    }

    function render() {
        dispose();
        entries.clear();
        list.replaceChildren();
        panel.hidden = !visible;
        if (!score) return;
        const chain = score.tracks[track];
        if (!chain) return;
        const ids = [chain[0], ...chain.slice(2)].filter(id => id >= 0 && score.nodes[id].product);
        for (const id of ids) {
            const details = document.createElement("details"), summary = document.createElement("summary");
            const name = document.createElement("span"), body = document.createElement("div");
            const entry = {id, details, body, generic: false};
            entries.set(id, entry);
            details.className = "plugin"; details.dataset.node = id;
            name.className = "plugin-name"; name.textContent = name.title = score.nodes[id].name;
            body.className = "plugin-body";
            summary.onclick = event => { if (entry.pending || busy) event.preventDefault(); };
            summary.append(name); details.append(summary, body); list.append(details);
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
            details.open = id === ids[0] && !native.has(id);
            details.ontoggle = () => {
                if (!details.open) detach(entry);
                else if (!entry.token) mount(entry).catch(fail);
            };
            buttons(entry);
            if (details.open) mount(entry).catch(fail);
        }
    }

    async function mount(entry) {
        if (entries.get(entry.id) !== entry || !running || busy || !visible || !entry.details.open || entry.pending) return;
        detach(entry);
        fail("");
        const id = entry.id, node = score.nodes[id];
        const token = entry.token = {revision: score.revision, ready: false};
        if (native.has(id)) {
            await request("watch", token.revision, id, "off");
            native.delete(id); buttons(entry);
            if (entry.token !== token) return;
        }
        const host = document.createElement("div"), shadow = host.attachShadow({mode: "open"});
        const style = document.createElement("style");
        style.textContent = `:host { display: block; font: inherit; }
            .perone-controls { display: grid; gap: 8px; }
            .perone-controls label { display: grid; grid-template-columns: minmax(5em, 1fr) minmax(4em, 1fr) 6em; gap: 6px; align-items: center; }
            .perone-controls input, .perone-controls select, .perone-controls meter { width: 100%; min-width: 0; }
            .perone-controls output { text-align: right; font-variant-numeric: tabular-nums; font-size: 12px; }
            button, input, select { font: inherit; }`;
        const element = document.createElement("div");
        shadow.append(style, element);
        entry.body.append(host);
        const edits = new Map(), messages = [];
        let sending = false;
        async function flush() {
            sending = true;
            try {
                while (entry.token === token && (edits.size || messages.length)) {
                    for (const index of [...edits.keys()]) {
                        if (entry.token !== token) return;
                        const value = edits.get(index);
                        edits.delete(index);
                        // One request in flight leaves room for status and DSP feedback between edits.
                        await request("parameter", token.revision, id, index, value);
                    }
                    if (entry.token === token && messages.length)
                        await request("message", token.revision, id, messages.shift());
                }
            } catch (error) { failed(entry, token, error); }
            finally { sending = false; }
        }
        const send = (op, ...args) => {
            if (entry.token !== token) return;
            if (op === "parameter") edits.set(args[0], args[1]);
            else if (messages.length < 64) messages.push(args[0]);
            else { failed(entry, token, Error("UI message queue full")); return; }
            if (token.ready && !sending) flush();
        };
        const parameter = (index, value) => {
            if (entry.token !== token) return;
            const p = node.product.parameters[index];
            if (!Number.isInteger(index) || !p || p.direction !== "input" || !Number.isFinite(value)) {
                failed(entry, token, Error("Invalid UI parameter")); return;
            }
            const low = p.isBypass ? 0 : p.minimum, high = p.isBypass ? 1 : p.maximum;
            const integer = p.integer || p.toggled || p.isBypass;
            value = integer ? Math.max(Math.ceil(low), Math.min(Math.floor(high), Math.round(value))) : Math.max(low, Math.min(high, value));
            send("parameter", index, value);
        };
        const callbacks = {product: node.product, set_parameter_begin: parameter,
            set_parameter: parameter, set_parameter_end: parameter,
            msg_write(bytes) {
                if (entry.token !== token) return;
                const limit = node.product.messaging?.uiToDspSize;
                if (!(bytes instanceof Uint8Array) || !limit || bytes.length > limit) {
                    failed(entry, token, Error("Invalid UI message")); return;
                }
                send("message", Array.from(bytes));
            }};
        try {
            const create = !entry.generic && node.product.ui?.web ?
                (await import(adapter.uiUrl(node, token.revision, id))).create : generic;
            if (entry.token !== token) return;
            const ui = await create(element, callbacks);
            if (!ui || typeof ui.free !== "function") throw Error("Perone UI must return free()");
            if (entry.token !== token) { ui.free(); return; }
            token.ui = ui;
            // No DSP stream until the factory is ready to receive it. Creation-time gestures stay queued.
            await request("watch", token.revision, id, "web");
            if (entry.token !== token) return;
            token.ready = true;
            if (edits.size || messages.length) flush();
            // The first poll supplies current values, including initial host overrides.
            await pollEntry(entry);
        } catch (error) { failed(entry, token, error); }
    }

    async function pollEntry(entry) {
        const token = entry.token;
        if (!token?.ready) return;
        try {
            const data = await request("controls", token.revision, entry.id);
            if (entry.token !== token) return;
            for (const [index, value] of data.values.entries()) {
                if (entry.token !== token) return;
                if (Number.isFinite(value)) token.ui.set_parameter?.(index, value);
            }
            for (const bytes of data.messages) {
                if (entry.token !== token) return;
                token.ui.msg_in?.(new Uint8Array(bytes));
            }
        } catch (error) { failed(entry, token, error); }
    }

    return {
        score(next, nativeAvailable = []) {
            score = next;
            available = new Set(nativeAvailable); native.clear();
            track = Math.min(track, Math.max(0, next.tracks.length - 1));
            render();
        },
        track(index) {
            if (index === track) show(true);
            else { track = index; visible = true; render(); }
        },
        show,
        windows(ids) {
            native = new Set(ids);
            for (const entry of entries.values()) buttons(entry);
        },
        status(value, pending) {
            const resumed = busy && !pending;
            running = value; busy = pending;
            if (!running) { dispose(); native.clear(); }
            for (const entry of entries.values()) {
                buttons(entry);
                if (running && resumed && !entry.token) mount(entry).catch(fail);
            }
        },
        dispose,
        async poll() { for (const entry of [...entries.values()]) await pollEntry(entry); }
    };
}
