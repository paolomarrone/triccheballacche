// One plugin's preset. Identity belongs to the document; payload bytes belong to its format adapter.
export const presetLimit = 16 * 1024 * 1024;
const hex = Array.from({length: 256}, (_, i) => i.toString(16).padStart(2, "0"));
export function encodeState(bytes) {
    if (bytes.length > presetLimit) throw Error("Preset exceeds 16 MiB");
    const parts = [];
    for (let i = 0; i < bytes.length; i += 4096) parts.push(Array.from(bytes.subarray(i, i + 4096), byte => hex[byte]).join(""));
    return parts.join("");
}
function validateState(data) {
    if (typeof data !== "string" || data.length > presetLimit * 2 || data.length % 2 || /[^0-9a-f]/.test(data))
        throw Error("Invalid preset data");
}
export function decodeState(data) {
    validateState(data);
    return Uint8Array.from({length: data.length / 2}, (_, i) => parseInt(data.slice(i * 2, i * 2 + 2), 16));
}
function identity(product) {
    return {format: product.format || "perone", plugin: product.id || product.bundleName};
}
export function writePreset(product, data) {
    validateState(data);
    return JSON.stringify({version: 1, ...identity(product), data}) + "\n";
}
export function readPreset(product, text) {
    if (text.length > presetLimit * 2 + 4096) throw Error("Preset file too large");
    const preset = JSON.parse(text), expected = identity(product);
    if (preset?.version !== 1 || preset.format !== expected.format || preset.plugin !== expected.plugin)
        throw Error("Preset belongs to a different plugin or version");
    validateState(preset.data);
    return preset.data;
}

export function download(name, text) {
    const url = URL.createObjectURL(new Blob([text], {type: "application/json"}));
    const link = document.createElement("a");
    link.href = url; link.download = name; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// Native WebViews need an explicit file destination; the browser uses Downloads.
export function destination(name) {
    return new Promise(resolve => {
        const dialog = document.createElement("dialog"), form = document.createElement("form");
        const label = document.createElement("label"), path = document.createElement("input");
        const save = document.createElement("button"), cancel = document.createElement("button");
        dialog.className = "preset-dialog";
        form.method = "dialog";
        label.textContent = "Export preset";
        path.value = name; path.required = true; path.setAttribute("aria-label", "Preset path");
        save.textContent = "Save"; save.value = "save";
        cancel.textContent = "Cancel"; cancel.type = "button"; cancel.onclick = () => dialog.close();
        label.append(path); form.append(label, cancel, save); dialog.append(form); document.body.append(dialog);
        dialog.onclose = () => { resolve(dialog.returnValue === "save" ? path.value : null); dialog.remove(); };
        dialog.showModal(); path.focus(); path.select();
    });
}
