import createModule from "../build/web/player.mjs";

// Each evaluation has its own VM and filesystem. The caller terminates runaway code.
self.onmessage = async ({data}) => {
    const log = [];
    try {
        const host = await createModule({printErr: message => log.push(message)});
        for (const {path, bytes} of data.files) {
            host.FS.mkdirTree(path.slice(0, path.lastIndexOf("/")) || "/");
            host.FS.writeFile(path, bytes);
        }
        const score = host.ccall("score_describe", "number", ["string", "string", "number"],
            [data.path, data.source, data.sampleRate]);
        if (!score) throw Error(log.join("\n") || "Score preparation failed");
        const pointer = host._score_pack_web(score), length = host._score_pack_length();
        if (!pointer) throw Error("Cannot transfer prepared score");
        const bytes = host.HEAPU8.slice(pointer, pointer + length);
        self.postMessage({bytes}, [bytes.buffer]);
    } catch (error) { self.postMessage({error: log.join("\n") || String(error)}); }
};
