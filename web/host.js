import {Perone} from "./perone.js";

export async function createHost(options = {}, factory) {
    factory ||= (await import("../build/web/daw.mjs")).default;
    const host = await factory(options);
    host.perone = new Perone(host);
    return host;
}

// Files are supplied by the caller. Janet reads metadata and imports from this filesystem.
export async function addFile(host, path, bytes) {
    path = host.perone.path(path);
    host.FS.mkdirTree(path.slice(0, path.lastIndexOf("/")) || "/");
    if (path.endsWith(".wasm")) await host.perone.add(path, bytes);
    host.FS.writeFile(path, bytes);
}

export function renderScore(host, path, sampleRate) {
    const score = host.ccall("score_new", "number", ["string", "number"], [path, sampleRate]);
    if (!score) throw Error("Score preparation failed; see Janet diagnostics");
    try {
        const frames = host._score_frames(score), audio = new Float32Array(frames * 2);
        const pointer = host._score_buffer(score) / 4;
        let peak = 0;
        for (let position = 0; position < audio.length;) {
            const n = host._score_render(score);
            if (n <= 0) throw Error("Audio rendering failed");
            const block = host.HEAPF32.subarray(pointer, pointer + n * 2);
            audio.set(block, position);
            for (const value of block) peak = Math.max(peak, Math.abs(value));
            position += n * 2;
        }
        const normalize = host._score_normalize(score);
        if (normalize && peak) for (let i = 0; i < audio.length; i++) audio[i] = (audio[i] / peak) * normalize;
        return audio;
    } finally {
        host._score_free(score);
    }
}
