import {Perone} from "./perone.js";

// Only prepares/disposes Perone instances. Miniaudio owns the audio processor.
registerProcessor("perone-setup", class extends AudioWorkletProcessor {
    constructor(options) {
        super();
        const host = globalThis.peroneHost;
        this.port.onmessage = ({data}) => {
            try {
                if (data.type !== "close") throw Error("Unknown worklet request");
                host.perone?.closeAll();
                this.port.postMessage({type: data.type});
            } catch (error) {
                this.port.postMessage({type: data.type, error: String(error)});
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
