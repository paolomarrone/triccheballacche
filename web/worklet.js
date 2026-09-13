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
                else if (data.type === "control") result = host.perone.control(...data.args);
                else throw Error("Unknown worklet request");
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
