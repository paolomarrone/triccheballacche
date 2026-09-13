// Perone ABI 2 host. Buffer transfer follows Tibia's web processor;
// typed callbacks follow tibia/test/perone_wasm.js (GPL-3.0-or-later).
// No product metadata is parsed here.
const names = ["alloc", "free", "init", "fini", "set_sample_rate", "mem_req", "mem_set", "reset", "process",
    "set_parameter", "get_parameter", "midi_msg_in", "set_transport", "msg_in", "state_save", "state_load"];
const callbackModule = new WebAssembly.Module(new Uint8Array([
    0, 97, 115, 109, 1, 0, 0, 0, 1, 6, 1, 0x60, 1, 0x7f, 1, 0x7f,
    2, 7, 1, 1, 104, 1, 102, 0, 0, 7, 5, 1, 1, 102, 0, 0
]));

const messageModule = new WebAssembly.Module(new Uint8Array([
    0, 97, 115, 109, 1, 0, 0, 0, 1, 7, 1, 0x60, 3, 0x7f, 0x7f, 0x7f, 0,
    2, 7, 1, 1, 104, 1, 102, 0, 0, 7, 5, 1, 1, 102, 0, 0
]));
const slots = 64;

// Fixed storage: DSP callbacks never allocate or post unbounded streams to the page.
class Messages {
    constructor(limit = 0) {
        this.limit = limit;
        this.data = new Uint8Array(limit * slots);
        this.sizes = new Uint32Array(slots);
        this.read = this.write = 0;
    }
    push(bytes) {
        if (!this.limit || bytes.length > this.limit || this.write - this.read === slots)
            throw Error("Perone message queue full or message too large");
        const i = this.write++ % slots;
        this.data.set(bytes, i * this.limit);
        this.sizes[i] = bytes.length;
    }
    drain() {
        const result = [];
        while (this.read !== this.write) {
            const i = this.read++ % slots;
            result.push(Array.from(this.data.subarray(i * this.limit, i * this.limit + this.sizes[i])));
        }
        return result;
    }
}

export class Perone {
    constructor(host) {
        this.host = host;
        this.modules = new Map();
        this.instances = new Map();
        this.remote = new Set(); // C handles whose DSP ownership moved to the worklet.
        this.next = 1;
    }

    path(path) {
        const parts = [];
        for (const part of (path.startsWith("/") ? path : this.host.FS.cwd() + "/" + path).split("/")) {
            if (part === "..") parts.pop();
            else if (part && part !== ".") parts.push(part);
        }
        return "/" + parts.join("/");
    }

    async add(path, bytes) {
        const module = await WebAssembly.compile(bytes);
        if (WebAssembly.Module.imports(module).length) throw Error("Perone requires a standalone wasm32 module");
        this.modules.set(this.path(path), module);
    }

    open(configuration, id = this.next++) {
        if (this.instances.has(id) || this.remote.has(id)) throw Error("Perone handle already in use");
        const config = {...configuration, path: this.path(configuration.path),
            parameters: new Float32Array(configuration.parameters)};
        const {path, sampleRate, inputChannels, outputChannels, midiBus, parameters, outputMask, capacity, toDsp = 0, toUi = 0} = config;
        const module = this.modules.get(path);
        if (!module) throw Error("Wasm module not preloaded: " + path);
        const wasm = new WebAssembly.Instance(module).exports;
        wasm.__wasm_call_ctors();
        const pointer = wasm.perone_get_api(2);
        if (!pointer) throw Error("Unsupported Perone ABI");
        const table = wasm.__indirect_function_table;
        const api = Object.fromEntries(Array.from(new Uint32Array(wasm.memory.buffer, pointer, names.length),
            (index, i) => [names[i], index ? table.get(index) : null]));
        const isOutput = i => (outputMask[i < 32 ? 0 : 1] >>> (i % 32)) & 1;
        const required = names.slice(0, 9);
        if (midiBus >= 0) required.push("midi_msg_in");
        if (toDsp) required.push("msg_in");
        parameters.forEach((_, i) => required.push(isOutput(i) ? "get_parameter" : "set_parameter"));
        for (const name of required) if (!api[name]) throw Error("Missing Perone function: " + name);

        const owned = [];
        const p = {wasm, api, owned, config, instance: 0, initialized: false, rendered: false,
            watching: false, pending: false, dirty: new Uint8Array(parameters.length), wanted: new Float32Array(parameters.length),
            toUi: new Messages(toUi), toDsp: new Messages(toDsp), overflow: false};
        const allocate = size => {
            const pointer = wasm.malloc(size) >>> 0;
            if (!pointer) throw Error("Perone allocation failed");
            owned.push(pointer);
            return pointer;
        };
        const words = values => {
            const pointer = allocate(values.length * 4);
            new Uint32Array(wasm.memory.buffer, pointer, values.length).set(values);
            return pointer;
        };
        const directory = value => {
            // TextEncoder is not available in AudioWorkletGlobalScope.
            const text = encodeURIComponent(value + "\0").replace(/%([0-9A-F]{2})/g,
                (_, hex) => String.fromCharCode(parseInt(hex, 16)));
            const bytes = Uint8Array.from(text, c => c.charCodeAt(0)), pointer = allocate(bytes.length);
            new Uint8Array(wasm.memory.buffer, pointer, bytes.length).set(bytes);
            const bridge = new WebAssembly.Instance(callbackModule, {h: {f: () => pointer}});
            const index = table.grow(1);
            table.set(index, bridge.exports.f);
            return index;
        };
        try {
            const bin = path.slice(0, path.lastIndexOf("/")), data = bin.slice(0, bin.lastIndexOf("/"));
            const bridge = new WebAssembly.Instance(messageModule, {h: {f: (_, size, pointer) => {
                if (!p.watching) return;
                try { p.toUi.push(new Uint8Array(wasm.memory.buffer, pointer, size)); }
                catch { p.overflow = true; }
            }}});
            const callback = table.grow(1);
            table.set(callback, bridge.exports.f);
            const callbacks = words([0, directory(bin), directory(data), callback]);
            p.instance = api.alloc() >>> 0;
            if (!p.instance || api.init(p.instance, callbacks)) throw Error("Perone initialization failed");
            p.initialized = true;
            parameters.forEach((value, i) => { if (!isOutput(i)) api.set_parameter(p.instance, i, value); });
            api.set_sample_rate(p.instance, sampleRate);
            const size = api.mem_req(p.instance) >>> 0;
            if (size) api.mem_set(p.instance, allocate(size));
            api.reset(p.instance);
            // Fixed capacity matches the C engine. Allocate before caching memory views.
            const x = Array.from({length: inputChannels}, () => allocate(capacity * 4));
            const y = Array.from({length: outputChannels}, () => allocate(capacity * 4));
            p.inputs = inputChannels ? words(x) : 0;
            p.outputs = words(y);
            const midi = allocate(3);
            p.messagePointer = toDsp ? allocate(toDsp) : 0;
            p.memory = wasm.memory.buffer;
            p.x = x.map(pointer => new Float32Array(p.memory, pointer, capacity));
            p.y = y.map(pointer => new Float32Array(p.memory, pointer, capacity));
            p.pointers = new Uint32Array(p.memory, p.inputs, inputChannels);
            p.addresses = x;
            p.midi = new Uint8Array(p.memory, midi, 3);
            p.midiPointer = midi;
            p.message = new Uint8Array(p.memory, p.messagePointer, toDsp);
            this.next = Math.max(this.next, id + 1);
            this.instances.set(id, p);
            return id;
        } catch (error) {
            this.dispose(p);
            throw error;
        }
    }

    dispose(p) {
        try {
            if (p.initialized) p.api.fini(p.instance);
        } finally {
            for (const pointer of p.owned) p.wasm.free(pointer);
            if (p.instance) p.api.free(p.instance);
        }
    }

    close(id) {
        const p = this.instances.get(id);
        if (p) {
            this.instances.delete(id);
            this.dispose(p);
        } else if (!this.remote.delete(id)) throw Error("Unknown Perone handle: " + id);
    }

    closeAll() {
        const errors = [];
        for (const id of this.instances.keys()) {
            try { this.close(id); } catch (error) { errors.push(error); }
        }
        if (errors.length) throw new AggregateError(errors, "Perone cleanup failed: " + errors.map(String).join("; "));
    }

    // Rebuild instructions for initial/reset state, never a snapshot of a running DSP.
    releasePrepared() {
        const instances = [...this.instances].map(([id, p]) => {
            if (p.rendered) throw Error("Cannot rebuild a DSP after rendering or MIDI input");
            return {id, config: p.config};
        });
        for (const {id} of instances) this.remote.add(id);
        this.closeAll();
        return {modules: [...this.modules], instances};
    }

    restorePrepared(setup) {
        if (this.instances.size) throw Error("Perone setup requires an empty host");
        this.modules = new Map(setup.modules);
        try {
            for (const {id, config} of setup.instances) this.open(config, id);
        } catch (error) {
            try { this.closeAll(); } catch (cleanup) {
                throw new AggregateError([error, cleanup], "Perone setup and cleanup failed: " + error + "; " + cleanup);
            }
            throw error;
        }
    }

    set(id, parameter, value) {
        const p = this.instances.get(id);
        p.config.parameters[parameter] = value;
        p.api.set_parameter(p.instance, parameter, value);
    }

    control(op, id, index, value) {
        const p = this.instances.get(id);
        if (!p) throw Error("Unknown Perone instance");
        if (op === "watch") {
            p.watching = Boolean(index);
            p.toUi.read = p.toUi.write;
            p.overflow = false;
        } else if (!p.watching) throw Error("Perone UI is not attached");
        else if (op === "parameter") {
            if (!Number.isInteger(index) || index < 0 || index >= p.config.parameters.length ||
                (p.config.outputMask[index < 32 ? 0 : 1] >>> (index % 32)) & 1 || !Number.isFinite(Math.fround(value)))
                throw Error("Invalid Perone parameter");
            p.wanted[index] = value;
            p.dirty[index] = 1;
            p.pending = true;
        } else if (op === "message") {
            if (!Array.isArray(index) || index.some(x => !Number.isInteger(x) || x < 0 || x > 255))
                throw Error("Invalid Perone message");
            p.toDsp.push(index);
            p.pending = true;
        } else if (op === "controls") {
            if (p.overflow) throw Error("Perone DSP message queue overflow");
            return {values: Array.from(p.config.parameters, (value, i) => p.dirty[i] ? null :
                ((p.config.outputMask[i < 32 ? 0 : 1] >>> (i % 32)) & 1) ? p.api.get_parameter(p.instance, i) : value),
                messages: p.toUi.drain()};
        } else throw Error("Unknown Perone control");
        return {};
    }

    // The scheduler calls this at the same boundary on native and Wasm, before score automation.
    sync(id, paired) {
        const p = this.instances.get(id);
        if (!p?.pending) return;
        p.pending = false;
        for (let i = 0; i < p.dirty.length; ++i) if (p.dirty[i]) {
            this.set(id, i, p.wanted[i]);
            if (paired) this.set(paired, i, p.wanted[i]);
            p.dirty[i] = 0;
        }
        while (p.toDsp.read !== p.toDsp.write) {
            const i = p.toDsp.read++ % slots, size = p.toDsp.sizes[i];
            const bytes = p.toDsp.data.subarray(i * p.toDsp.limit, i * p.toDsp.limit + size);
            p.message.set(bytes);
            p.api.msg_in(p.instance, size, p.messagePointer);
            if (paired) {
                const other = this.instances.get(paired);
                other.message.set(bytes);
                other.api.msg_in(other.instance, size, other.messagePointer);
            }
        }
    }

    reset(id) {
        const p = this.instances.get(id);
        p.api.reset(p.instance);
    }

    midi(id, bus, pointer) {
        const p = this.instances.get(id), heap = this.host.HEAPU8;
        p.rendered = true;
        for (let i = 0; i < 3; i++) p.midi[i] = heap[pointer + i];
        p.api.midi_msg_in(p.instance, bus, p.midiPointer);
    }

    process(id, inputs, outputs, frames) {
        const p = this.instances.get(id), heap = this.host.HEAPF32, pointers = this.host.HEAPU32;
        if (frames > p.config.capacity || p.memory !== p.wasm.memory.buffer) throw Error("Invalid DSP buffer or memory growth");
        p.rendered = true;
        for (let c = 0; c < p.x.length; c++) {
            const pointer = inputs ? pointers[inputs / 4 + c] : 0;
            p.pointers[c] = pointer ? p.addresses[c] : 0;
            if (pointer) for (let i = 0; i < frames; i++) p.x[c][i] = heap[pointer / 4 + i];
        }
        p.api.process(p.instance, p.inputs, p.outputs, frames);
        if (p.memory !== p.wasm.memory.buffer) throw Error("DSP grew memory during processing");
        for (let c = 0; c < p.y.length; c++) {
            const pointer = pointers[outputs / 4 + c] / 4;
            for (let i = 0; i < frames; i++) heap[pointer + i] = p.y[c][i];
        }
    }
}
