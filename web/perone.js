// Perone ABI 2 host. Buffer transfer follows Tibia's web processor;
// typed callbacks follow tibia/test/perone_wasm.js (GPL-3.0-or-later).
// No product metadata is parsed here.
const names = ["alloc", "free", "init", "fini", "set_sample_rate", "mem_req", "mem_set", "reset", "process",
    "set_parameter", "get_parameter", "midi_msg_in", "set_transport", "msg_in", "state_save", "state_load"];
const callbackModule = new WebAssembly.Module(new Uint8Array([
    0, 97, 115, 109, 1, 0, 0, 0, 1, 6, 1, 0x60, 1, 0x7f, 1, 0x7f,
    2, 7, 1, 1, 104, 1, 102, 0, 0, 7, 5, 1, 1, 102, 0, 0
]));

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
        const {path, sampleRate, inputChannels, outputChannels, midiBus, parameters, outputMask, capacity} = config;
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
        parameters.forEach((_, i) => required.push(isOutput(i) ? "get_parameter" : "set_parameter"));
        for (const name of required) if (!api[name]) throw Error("Missing Perone function: " + name);

        const owned = [];
        const p = {wasm, api, owned, config, instance: 0, initialized: false, rendered: false};
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
            const callbacks = words([0, directory(bin), directory(data), 0]);
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
            p.memory = wasm.memory.buffer;
            p.x = x.map(pointer => new Float32Array(p.memory, pointer, capacity));
            p.y = y.map(pointer => new Float32Array(p.memory, pointer, capacity));
            p.pointers = new Uint32Array(p.memory, p.inputs, inputChannels);
            p.addresses = x;
            p.midi = new Uint8Array(p.memory, midi, 3);
            p.midiPointer = midi;
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
