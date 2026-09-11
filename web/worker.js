import {createHost, addFile, renderScore} from "./host.js";

// One render per worker. The caller can terminate it to cancel Janet or DSP execution.
self.onmessage = async ({data}) => {
    self.onmessage = null;
    try {
        const host = await createHost({printErr: message => self.postMessage({log: message})});
        for (const {path, url} of data.files) {
            const response = await fetch(url);
            if (!response.ok) throw Error(`Cannot load ${url}: ${response.status}`);
            await addFile(host, path, new Uint8Array(await response.arrayBuffer()));
        }
        const audio = renderScore(host, data.score, data.sampleRate);
        self.postMessage({audio, sampleRate: data.sampleRate}, [audio.buffer]);
    } catch (error) {
        self.postMessage({error: String(error)});
    }
};
