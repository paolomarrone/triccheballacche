import {create as generic} from "./perone-ui.js";

// One mounted view, independent of the audio backend. The product and indices remain Perone's.
export function plugins(request, adapter, fail) {
    const panel = document.getElementById("plugins"), select = document.getElementById("plugin-node");
    const kind = document.getElementById("plugin-kind"), container = document.getElementById("plugin-ui");
    let score, running = false, visible = false, current, generation = 0;
    if (adapter.nativeViews) kind.add(new Option("Nativa", "native"));

    function dispose() {
        ++generation;
        const old = current;
        current = undefined; // Ignore callbacks emitted by free(), including gesture ends.
        try { old?.ui?.free(); }
        catch (error) { fail(error); }
        container.replaceChildren();
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
        await request("watch", score.revision, id, native);
        if (serial !== generation) return;
        const token = current = {id, revision: score.revision, native};
        if (native) return;
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
        const send = (op, ...args) => {
            if (current === token) request(op, token.revision, id, ...args).catch(error => { if (current === token) fail(error); });
        };
        const parameter = (index, value) => {
            if (current !== token) return;
            const p = node.product.parameters[index];
            if (!Number.isInteger(index) || !p || p.direction !== "input" || !Number.isFinite(value)) {
                fail(Error("Parametro della GUI non valido")); return;
            }
            const low = p.isBypass ? 0 : p.minimum, high = p.isBypass ? 1 : p.maximum;
            const integer = p.integer || p.toggled || p.isBypass;
            value = integer ? Math.max(Math.ceil(low), Math.min(Math.floor(high), Math.round(value))) : Math.max(low, Math.min(high, value));
            send("parameter", index, value);
        };
        const callbacks = {product: node.product, set_parameter_begin: parameter,
            set_parameter: parameter, set_parameter_end: parameter,
            msg_write(bytes) {
                if (!(bytes instanceof Uint8Array) || bytes.length > (node.product.messaging?.uiToDspSize || 0)) {
                    fail(Error("Messaggio della GUI non valido")); return;
                }
                send("message", Array.from(bytes));
            }};
        try {
            const create = kind.value !== "generic" && node.product.ui?.web ?
                (await import(adapter.uiUrl(node, score.revision, id))).create : generic;
            if (current !== token) return;
            const ui = await create(element, callbacks);
            if (!ui || typeof ui.free !== "function") throw Error("La GUI Perone deve restituire free()");
            if (current !== token) { ui.free(); return; }
            token.ui = ui;
            // The first poll supplies current values, including initial host overrides.
            await poll();
        } catch (error) {
            if (current === token) {
                dispose();
                await request("watch", token.revision, -1);
                throw error;
            }
        }
    }

    async function poll() {
        const token = current;
        if (!token || token.native) return;
        try {
            const data = await request("controls", token.revision, token.id);
            if (current !== token) return;
            data.values.forEach((value, index) => {
                if (Number.isFinite(value)) token.ui?.set_parameter?.(index, value);
            });
            for (const bytes of data.messages) token.ui?.msg_in?.(new Uint8Array(bytes));
        } catch (error) {
            if (current !== token) return;
            dispose();
            // The player may already have ended; cleanup errors must not hide the UI failure.
            await request("watch", token.revision, -1).catch(() => {});
            fail(error);
        }
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
