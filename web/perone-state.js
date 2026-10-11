// The PER1 envelope is shared with host/perone.c; custom payloads belong to the plugin.
const limit = 64 * 1024 * 1024, callbackModules = new Map();

export function callback(table, parameters, result, fn) {
    const key = parameters.join(",") + ":" + result;
    let module = callbackModules.get(key);
    if (!module) {
        const type = [1, 0x60, parameters.length, ...parameters, result ? 1 : 0, ...(result ? [result] : [])];
        module = new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0,
            1, type.length, ...type, 2, 7, 1, 1, 104, 1, 102, 0, 0, 7, 5, 1, 1, 102, 0, 0]));
        callbackModules.set(key, module);
    }
    const bridge = new WebAssembly.Instance(module, {h: {f: fn}}), index = table.grow(1);
    table.set(index, bridge.exports.f);
    return index;
}

function controls(p, values, index, value) {
    const c = p.config;
    if (!Number.isInteger(index) || index < 0 || index >= c.parameters.length || c.outputParameters[index] || !Number.isFinite(value))
        throw Error("Invalid state parameter");
    let low = c.minimum?.[index] ?? -Infinity, high = c.maximum?.[index] ?? Infinity;
    if (c.flags?.[index] & 2) {
        value = Math.sign(value) * Math.floor(Math.abs(value) + .5); low = Math.ceil(low); high = Math.floor(high);
    }
    values.set(index, Math.max(low, Math.min(high, value)));
}

function callbacks(p) {
    if (p.stateCallbacks) return p.stateCallbacks;
    const table = p.wasm.__indirect_function_table;
    const lock = callback(table, [0x7f], 0, () => {}); // Audio is stopped by the caller.
    const write = callback(table, [0x7f, 0x7f, 0x7f], 0x7f, (_, data, size) => {
        const s = p.state;
        try {
            if (!s.writing || size < 0 || size > limit - s.length) throw Error("Plugin state too large");
            s.parts.push(new Uint8Array(p.wasm.memory.buffer, data, size).slice());
            s.length += size;
            return 0;
        } catch { s.failed = true; return -1; }
    });
    const set = callback(table, [0x7f, 0x7f, 0x7d], 0, (_, index, value) => {
        try {
            if (p.state.writing) throw Error("Cannot change parameters while saving state");
            controls(p, p.state.values, index, value);
        }
        catch { p.state.failed = true; }
    });
    const pointer = p.wasm.malloc(20) >>> 0;
    if (!pointer) throw Error("Perone allocation failed");
    p.owned.push(pointer);
    new Uint32Array(p.wasm.memory.buffer, pointer, 5).set([0, lock, lock, write, set]);
    return p.stateCallbacks = pointer;
}

export function peroneState(p, bytes) {
    const custom = !!p.api.state_save, loading = bytes !== undefined;
    if (custom !== !!p.api.state_load) throw Error("Plugin state unavailable");
    if (loading) {
        if (!(bytes instanceof Uint8Array) || bytes.length < 8 || bytes.length > limit) throw Error("Invalid plugin state");
        const header = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        if (header.getUint32(0, true) !== 0x31524550 || header.getUint32(4, true) !== Number(custom))
            throw Error("Incompatible Perone state");
    }
    const s = p.state = {parts: [], length: 8, values: new Map(), writing: !loading, failed: false};
    let pointer = 0;
    try {
        if (custom) {
            const cb = callbacks(p);
            let result;
            if (loading) {
                pointer = p.wasm.malloc(Math.max(1, bytes.length - 8)) >>> 0;
                if (!pointer) throw Error("Perone allocation failed");
                new Uint8Array(p.wasm.memory.buffer, pointer, bytes.length - 8).set(bytes.subarray(8));
                result = p.api.state_load(cb, p.config.sampleRate, pointer, bytes.length - 8);
            } else result = p.api.state_save(p.instance, cb, p.config.sampleRate);
            if (result || s.failed) throw Error("Perone state operation failed");
        } else if (loading) {
            if (bytes.length !== 8 + p.config.parameters.length * 4) throw Error("Invalid parameter state");
            const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
            p.config.parameters.forEach((_, i) => {
                if (!p.config.outputParameters[i]) controls(p, s.values, i, view.getFloat32(8 + i * 4, true));
            });
        } else {
            const data = new Uint8Array(p.config.parameters.length * 4), view = new DataView(data.buffer);
            p.config.parameters.forEach((value, i) => view.setFloat32(i * 4, p.config.outputParameters[i] ? 0 : value, true));
            s.parts.push(data); s.length += data.length;
        }
        if (loading) return s.values;
        const data = new Uint8Array(s.length), header = new DataView(data.buffer);
        header.setUint32(0, 0x31524550, true); header.setUint32(4, Number(custom), true);
        let offset = 8;
        for (const part of s.parts) { data.set(part, offset); offset += part.length; }
        return data;
    } finally {
        if (pointer) p.wasm.free(pointer);
        p.state = undefined;
    }
}
