// One UI's parameter/message exchange. The panel owns its factory and lifetime;
// this object owns edits, gestures and feedback, independently of the DOM or backend.
export function pluginControls(product, request, fail) {
    const edits = new Map(), gestures = new Set(), messages = [];
    const changes = product.parameters.map(() => 0), uninitialized = new Set(changes.keys());
    let ui, closed = false, sending = false;

    async function flush() {
        sending = true;
        try {
            while (!closed && (edits.size || messages.length)) {
                for (const index of [...edits.keys()]) {
                    if (closed) return;
                    const value = edits.get(index), change = changes[index];
                    // One request in flight leaves room for status and feedback between edits.
                    await request("parameter", index, value);
                    if (changes[index] === change) edits.delete(index);
                }
                if (!closed && messages.length) await request("message", messages.shift());
            }
        } catch (error) { if (!closed) fail(error); }
        finally { sending = false; }
    }

    function send(op, ...args) {
        if (closed) return;
        if (op === "parameter") { ++changes[args[0]]; edits.set(args[0], args[1]); }
        else if (messages.length < 64) messages.push(args[0]);
        else { fail(Error("UI message queue full")); return; }
        if (ui && !sending) flush();
    }

    function parameter(index, value) {
        if (closed) return;
        const p = product.parameters[index];
        if (!Number.isInteger(index) || !p || p.direction !== "input" || !Number.isFinite(value)) {
            fail(Error("Invalid UI parameter")); return;
        }
        const low = p.isBypass ? 0 : p.minimum, high = p.isBypass ? 1 : p.maximum;
        const integer = p.integer || p.toggled || p.isBypass;
        value = integer ? Math.max(Math.ceil(low), Math.min(Math.floor(high), Math.round(value))) : Math.max(low, Math.min(high, value));
        send("parameter", index, value);
    }

    return {
        callbacks: {product,
            set_parameter_begin(index, value) { if (!closed) { gestures.add(index); parameter(index, value); } },
            set_parameter: parameter,
            set_parameter_end(index, value) { if (!closed) { parameter(index, value); gestures.delete(index); } },
            msg_write(bytes) {
                if (closed) return;
                if (!(bytes instanceof Uint8Array) || !product.messaging?.uiToDspSize || bytes.length > product.messaging.uiToDspSize) {
                    fail(Error("Invalid UI message")); return;
                }
                send("message", Array.from(bytes));
            }},
        attach(view) {
            if (closed) return;
            ui = view;
            if (edits.size || messages.length) flush();
        },
        async poll() {
            if (!ui || closed) return false;
            const before = changes.slice(), pending = new Set(edits.keys());
            try {
                const data = await request("controls");
                if (closed) return false;
                for (const [index, value] of data.values.entries()) {
                    if (closed) return false;
                    // Deliver only feedback taken after the latest local edit.
                    if (Number.isFinite(value) && !gestures.has(index) && !pending.has(index) &&
                        !edits.has(index) && changes[index] === before[index]) {
                        ui.set_parameter?.(index, value);
                        uninitialized.delete(index);
                    }
                }
                for (const bytes of data.messages) {
                    if (closed) return false;
                    ui.msg_in?.(new Uint8Array(bytes));
                }
                return !closed && !uninitialized.size;
            } catch (error) { if (!closed) fail(error); return false; }
        },
        close() { closed = true; ui = undefined; edits.clear(); messages.length = 0; }
    };
}
