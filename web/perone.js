import {callback, peroneState} from "./perone-state.js";

// Perone ABI 2 host. Buffer transfer follows Tibia's web processor;
// typed callbacks follow tibia/test/perone_wasm.js (GPL-3.0-or-later).
// No product metadata is parsed here.
const names = ["alloc", "free", "init", "fini", "set_sample_rate", "mem_req", "mem_set", "reset", "process",
    "set_parameter", "get_parameter", "midi_msg_in", "set_transport", "msg_in", "state_save", "state_load"];
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
        this.planned = new Map(); // Configuration only until the audio owner instantiates it.
        this.deferred = false;
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
        if (this.instances.has(id) || this.planned.has(id) || this.remote.has(id)) throw Error("Perone handle already in use");
        const config = {...configuration, path: this.path(configuration.path),
            parameters: new Float32Array(configuration.parameters)};
        const {path, sampleRate, inputChannels, outputChannels, midiBus, parameters, outputParameters, capacity, toDsp = 0, toUi = 0} = config;
        const module = this.modules.get(path);
        if (!module) throw Error("Wasm module not preloaded: " + path);
        if (this.deferred) {
            this.planned.set(id, config);
            this.next = Math.max(this.next, id + 1);
            return id;
        }
        const wasm = new WebAssembly.Instance(module).exports;
        wasm.__wasm_call_ctors();
        const pointer = wasm.perone_get_api(2);
        if (!pointer) throw Error("Unsupported Perone ABI");
        const table = wasm.__indirect_function_table;
        const api = Object.fromEntries(Array.from(new Uint32Array(wasm.memory.buffer, pointer, names.length),
            (index, i) => [names[i], index ? table.get(index) : null]));
        const isOutput = i => outputParameters[i];
        const required = names.slice(0, 9);
        if (midiBus >= 0) required.push("midi_msg_in");
        if (toDsp) required.push("msg_in");
        parameters.forEach((_, i) => required.push(isOutput(i) ? "get_parameter" : "set_parameter"));
        for (const name of required) if (!api[name]) throw Error("Missing Perone function: " + name);

        const owned = [];
        const p = {wasm, api, owned, config, instance: 0, initialized: false,
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
            return callback(table, [0x7f], 0x7f, () => pointer);
        };
        try {
            const bin = path.slice(0, path.lastIndexOf("/")), data = bin.slice(0, bin.lastIndexOf("/"));
            const message = callback(table, [0x7f, 0x7f, 0x7f], 0, (_, size, pointer) => {
                if (!p.watching) return;
                try { p.toUi.push(new Uint8Array(wasm.memory.buffer, pointer, size)); }
                catch { p.overflow = true; }
            });
            const callbacks = words([0, directory(bin), directory(data), message]);
            p.instance = api.alloc() >>> 0;
            if (!p.instance || api.init(p.instance, callbacks)) throw Error("Perone initialization failed");
            p.initialized = true;
            api.set_sample_rate(p.instance, sampleRate);
            parameters.forEach((value, i) => { if (!isOutput(i)) api.set_parameter(p.instance, i, value); });
            const size = api.mem_req(p.instance) >>> 0;
            if (size) api.mem_set(p.instance, allocate(size));
            api.reset(p.instance);
            // Fixed capacity matches the C engine. Allocate before caching memory views.
            const x = Array.from({length: inputChannels}, () => allocate(capacity * 4));
            const y = Array.from({length: outputChannels}, () => allocate(capacity * 4));
            p.inputs = inputChannels ? words(x) : 0;
            p.outputs = words(y);
            const midi = allocate(3);
            p.transportPointer = api.set_transport ? allocate(64) : 0;
            p.messagePointer = toDsp ? allocate(toDsp) : 0;
            p.addresses = x; p.outputAddresses = y;
            p.midiPointer = midi;
            this.views(p);
            if (p.transport) new Uint8Array(p.memory, p.transportPointer, 64).fill(0);
            this.next = Math.max(this.next, id + 1);
            this.instances.set(id, p);
            return id;
        } catch (error) {
            this.dispose(p);
            throw error;
        }
    }

    views(p) {
        const {capacity, inputChannels, toDsp = 0} = p.config;
        p.memory = p.wasm.memory.buffer;
        p.x = p.addresses.map(pointer => new Float32Array(p.memory, pointer, capacity));
        p.y = p.outputAddresses.map(pointer => new Float32Array(p.memory, pointer, capacity));
        p.pointers = new Uint32Array(p.memory, p.inputs, inputChannels);
        p.midi = new Uint8Array(p.memory, p.midiPointer, 3);
        p.message = new Uint8Array(p.memory, p.messagePointer, toDsp);
        p.transport = p.transportPointer ? new DataView(p.memory, p.transportPointer, 64) : null;
    }

    state(id, bytes) {
        const p = this.instances.get(id);
        if (!p) throw Error("Unknown Perone instance");
        this.sync(id);
        try {
            const result = peroneState(p, bytes);
            if (bytes === undefined) return result;
            for (const [index, value] of result) this.set(id, index, value);
        } finally { this.views(p); }
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
        if (this.planned.delete(id)) return;
        const p = this.instances.get(id);
        if (p) {
            this.instances.delete(id);
            this.dispose(p);
        } else if (!this.remote.delete(id)) throw Error("Unknown Perone handle: " + id);
    }

    closeAll() {
        this.planned.clear();
        const errors = [];
        for (const id of this.instances.keys()) {
            try { this.close(id); } catch (error) { errors.push(error); }
        }
        if (errors.length) throw new AggregateError(errors, "Perone cleanup failed: " + errors.map(String).join("; "));
    }

    // Transfer descriptions only. Each DSP is instantiated once by its audio owner.
    releasePrepared() {
        if (this.instances.size) throw Error("Only deferred DSPs can be transferred");
        const instances = [...this.planned].map(([id, config]) => ({id, config}));
        const paths = new Set(instances.map(({config}) => config.path));
        for (const {id} of instances) this.remote.add(id);
        this.planned.clear();
        return {modules: [...this.modules].filter(([path]) => paths.has(path)), instances};
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
        const config = this.planned.get(id);
        if (config) { config.parameters[parameter] = value; return; }
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
                p.config.outputParameters[index] || !Number.isFinite(Math.fround(value)))
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
                p.config.outputParameters[i] ? p.api.get_parameter(p.instance, i) : value),
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
        if (this.planned.has(id)) return;
        const p = this.instances.get(id);
        p.pending = p.overflow = false;
        p.dirty.fill(0);
        p.toDsp.read = p.toDsp.write;
        p.toUi.read = p.toUi.write;
        p.api.reset(p.instance);
    }

    midi(id, bus, pointer) {
        const p = this.instances.get(id), heap = this.host.HEAPU8;
        for (let i = 0; i < 3; i++) p.midi[i] = heap[pointer + i];
        p.api.midi_msg_in(p.instance, bus, p.midiPointer);
    }

    transport(id, playing, bpm, beat) {
        const p = this.instances.get(id);
        if (!p?.transport || playing < 0) return;
        // Perone ABI v2, wasm32 layout. Only playing, BPM and quarter position are valid.
        const t = p.transport;
        t.setUint32(0, 13, true); t.setUint32(4, 13, true);
        t.setUint8(8, playing ? 1 : 0);
        t.setFloat32(16, bpm, true); t.setFloat64(24, beat, true);
        p.api.set_transport(p.instance, p.transportPointer);
    }

    process(id, inputs, outputs, frames) {
        const p = this.instances.get(id), heap = this.host.HEAPF32, pointers = this.host.HEAPU32;
        if (frames > p.config.capacity || p.memory !== p.wasm.memory.buffer) throw Error("Invalid DSP buffer or memory growth");
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
