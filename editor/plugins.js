import {create as generic} from "./perone-ui.js";

// One mounted view, independent of the audio backend. The product and indices remain Perone's.
export function plugins(request, adapter, fail) {
    const panel = document.getElementById("plugins"), select = document.getElementById("plugin-node");
    const kind = document.getElementById("plugin-kind"), container = document.getElementById("plugin-ui");
    let score, running = false, visible = false, current, generation = 0;
    if (adapter.nativeViews) kind.add(new Option("Native", "native"));

    function dispose() {
        ++generation;
        const old = current;
        current = undefined; // Ignore callbacks emitted by free(), including gesture ends.
        try { old?.ui?.free(); }
        catch (error) { fail(error); }
        container.replaceChildren();
    }

    function failed(token, error) {
        if (current !== token) return;
        dispose();
        fail(error);
        // Teardown may race Stop; its reply must not hide the original failure.
        request("watch", token.revision, -1).catch(() => {});
    }

    async function mount() {
        dispose();
        fail("");
        panel.hidden = !visible;
        if (!running || !score) return;
        const serial = generation;
        await request("watch", score.revision, -1);
        if (serial !== generation || !visible || !select.options.length) return;
        const id = Number(select.value), node = score.nodes[id], native = kind.value === "native";
        const token = current = {id, revision: score.revision, ready: false};
        if (native) {
            try { await request("watch", token.revision, id, true); }
            catch (error) { failed(token, error); }
            return;
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
        container.append(host);
        const edits = new Map(), messages = [];
        let sending = false;
        async function flush() {
            sending = true;
            try {
                while (current === token && (edits.size || messages.length)) {
                    for (const index of [...edits.keys()]) {
                        if (current !== token) return;
                        const value = edits.get(index);
                        edits.delete(index);
                        // One request in flight leaves room for status and DSP feedback between edits.
                        await request("parameter", token.revision, id, index, value);
                    }
                    if (current === token && messages.length)
                        await request("message", token.revision, id, messages.shift());
                }
            } catch (error) { failed(token, error); }
            finally { sending = false; }
        }
        const send = (op, ...args) => {
            if (current !== token) return;
            if (op === "parameter") edits.set(args[0], args[1]);
            else if (messages.length < 64) messages.push(args[0]);
            else { failed(token, Error("UI message queue full")); return; }
            if (token.ready && !sending) flush();
        };
        const parameter = (index, value) => {
            if (current !== token) return;
            const p = node.product.parameters[index];
            if (!Number.isInteger(index) || !p || p.direction !== "input" || !Number.isFinite(value)) {
                failed(token, Error("Invalid UI parameter")); return;
            }
            const low = p.isBypass ? 0 : p.minimum, high = p.isBypass ? 1 : p.maximum;
            const integer = p.integer || p.toggled || p.isBypass;
            value = integer ? Math.max(Math.ceil(low), Math.min(Math.floor(high), Math.round(value))) : Math.max(low, Math.min(high, value));
            send("parameter", index, value);
        };
        const callbacks = {product: node.product, set_parameter_begin: parameter,
            set_parameter: parameter, set_parameter_end: parameter,
            msg_write(bytes) {
                if (current !== token) return;
                const limit = node.product.messaging?.uiToDspSize;
                if (!(bytes instanceof Uint8Array) || !limit || bytes.length > limit) {
                    failed(token, Error("Invalid UI message")); return;
                }
                send("message", Array.from(bytes));
            }};
        try {
            const create = kind.value !== "generic" && node.product.ui?.web ?
                (await import(adapter.uiUrl(node, token.revision, id))).create : generic;
            if (current !== token) return;
            const ui = await create(element, callbacks);
            if (!ui || typeof ui.free !== "function") throw Error("Perone UI must return free()");
            if (current !== token) { ui.free(); return; }
            token.ui = ui;
            // No DSP stream until the factory is ready to receive it. Creation-time gestures stay queued.
            await request("watch", token.revision, id, false);
            if (current !== token) return;
            token.ready = true;
            if (edits.size || messages.length) flush();
            // The first poll supplies current values, including initial host overrides.
            await poll();
        } catch (error) { failed(token, error); }
    }

    async function poll() {
        const token = current;
        if (!token?.ready) return;
        try {
            const data = await request("controls", token.revision, token.id);
            if (current !== token) return;
            for (const [index, value] of data.values.entries()) {
                if (current !== token) return;
                if (Number.isFinite(value)) token.ui.set_parameter?.(index, value);
            }
            for (const bytes of data.messages) {
                if (current !== token) return;
                token.ui.msg_in?.(new Uint8Array(bytes));
            }
        } catch (error) { failed(token, error); }
    }

    for (const input of [select, kind]) input.addEventListener("change", () => mount().catch(fail));
    return {
        async score(next) {
            score = next;
            select.replaceChildren(...next.nodes.flatMap((node, id) => node.product ? [new Option(`${id} · ${node.name}`, id)] : []));
            await mount();
        },
        async show(value) { visible = value; await mount(); },
        status(value, busy) {
            running = value;
            select.disabled = kind.disabled = !running || busy;
            if (!running && current) dispose();
        },
        dispose,
        poll
    };
}
