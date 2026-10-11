import {Perone} from "./perone.js";

// Perone lifecycle and bounded UI exchange. Miniaudio owns the audio processor.
registerProcessor("perone-setup", class extends AudioWorkletProcessor {
    constructor(options) {
        super();
        const host = globalThis.peroneHost;
        this.port.onmessage = ({data}) => {
            try {
                let result;
                if (data.type === "close") host.perone?.closeAll();
                else if (data.type === "control") {
                    result = host.perone.control(...data.args);
                    if (data.player) host._player_sync(data.player);
                } else if (data.type === "state") {
                    const loading = data.data !== undefined;
                    let pointer = host._malloc(loading ? Math.max(1, data.data.length) : 4), bytes = 0;
                    if (!pointer) throw Error("Out of memory");
                    try {
                        if (loading) {
                            host.HEAPU8.set(data.data, pointer);
                            if (host._score_state_load(data.score, data.node, pointer, data.data.length))
                                throw Error("Cannot restore plugin state");
                        } else {
                            bytes = host._score_state_save(data.score, data.node, pointer);
                            if (!bytes) throw Error("Plugin state unavailable or export failed");
                            result = host.HEAPU8.slice(bytes, bytes + host.HEAPU32[pointer / 4]);
                        }
                    } finally { host._free(bytes); host._free(pointer); }
                } else if (data.type === "seek") {
                    if (host._player_seek(data.player, data.seconds)) throw Error("Cannot seek score");
                } else throw Error("Unknown worklet request");
                this.port.postMessage({type: data.type, id: data.id, result});
            } catch (error) {
                this.port.postMessage({type: data.type, id: data.id, error: String(error)});
            }
        };
        try {
            if (host.perone) throw Error("Worklet already prepared");
            host.perone = new Perone(host);
            host.perone.restorePrepared(options.processorOptions);
            this.port.postMessage({type: "ready"});
        } catch (error) {
            this.port.postMessage({type: "ready", error: String(error)});
        }
    }

    process() { return false; }
});
