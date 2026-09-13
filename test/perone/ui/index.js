import {label} from "./label.js";

// A real custom Perone UI contract, including relative assets and its own non-DSP Wasm.
export async function create(element, callbacks) {
    const css = document.createElement("link");
    css.rel = "stylesheet";
    css.href = new URL("./style.css", import.meta.url);
    const {instance} = await WebAssembly.instantiateStreaming(fetch(new URL("./helper.wasm", import.meta.url)),
        {helper: {value: () => 7}});
    if (globalThis.fixtureWait) await new Promise(resolve => { globalThis.fixtureResume = resolve; });
    const root = document.createElement("div");
    root.className = "fixture-ui";
    root.dataset.helper = instance.exports.value();
    const gain = document.createElement("button"), message = document.createElement("button"), out = document.createElement("output");
    gain.textContent = label; gain.id = "set-gain";
    message.textContent = "Messaggio"; message.id = "send-message";
    const index = callbacks.product.parameters.findIndex(p => p.id === "gain");
    gain.onclick = () => {
        callbacks.set_parameter_begin(index, .3);
        callbacks.set_parameter(index, .3);
        callbacks.set_parameter_end(index, .3);
    };
    message.onclick = () => callbacks.msg_write(new Uint8Array([0, 127, 255]));
    root.append(gain, message, out);
    element.append(css, root);
    // Tests retain callbacks to verify they cannot touch a later instance after free().
    (globalThis.fixtureCallbacks ||= []).push(callbacks);
    return {
        set_parameter(i, value) { if (i === index) root.dataset.gain = value; else if (i === 0) root.dataset.meter = value; },
        msg_in(bytes) { out.textContent = Array.from(bytes).join(","); },
        free() { root.remove(); css.remove(); globalThis.fixtureFreed = (globalThis.fixtureFreed || 0) + 1; }
    };
}
