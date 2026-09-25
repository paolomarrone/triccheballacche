// Only source and metadata are needed for preparation; DSP/UI binaries stay in the playback host.
export function prepareBackground(host, path, source, sampleRate) {
    const files = [];
    function collect(directory) {
        for (const name of host.FS.readdir(directory)) {
            if (name.startsWith(".")) continue;
            const file = `${directory}/${name}`.replace(/^\/\//, "/");
            const stat = host.FS.stat(file);
            if (host.FS.isDir(stat.mode)) {
                if (!["/dev", "/proc", "/tmp"].includes(file)) collect(file);
            } else if (host.FS.isFile(stat.mode) && !name.endsWith(".wasm")) {
                files.push({path: file, bytes: host.FS.readFile(file)});
            }
        }
    }
    collect("/");
    return new Promise((resolve, reject) => {
        const worker = new Worker(new URL("./prepare-worker.js", import.meta.url), {type: "module"});
        const finish = (error, value) => {
            clearTimeout(timeout);
            worker.terminate();
            error ? reject(error) : resolve(value);
        };
        const timeout = setTimeout(() => finish(Error("Score preparation exceeded the time limit")), 5000);
        worker.onerror = event => finish(Error(event.message));
        worker.onmessage = ({data}) => {
            if (data.error) return finish(Error(data.error));
            const pointer = host._malloc(data.bytes.length);
            if (!pointer) return finish(Error("Out of memory"));
            try {
                host.HEAPU8.set(data.bytes, pointer);
                const score = host._score_import(pointer, data.bytes.length);
                if (!score) throw Error("Invalid prepared score");
                finish(null, score);
            } catch (error) { finish(error); }
            finally { host._free(pointer); }
        };
        try { worker.postMessage({files, path, source, sampleRate}, files.map(file => file.bytes.buffer)); }
        catch (error) { finish(error); }
    });
}
