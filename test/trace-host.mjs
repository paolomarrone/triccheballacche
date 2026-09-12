import {addFile} from "../web/host.js";

const encoder = new TextEncoder();
export const entry = "/trace-run.janet";
const output = "/trace.json";

// The user's file stays at its original path, preserving imports and source coordinates.
export async function traceScore(host, path, source) {
    await addFile(host, path, encoder.encode(source));
    if (host.FS.analyzePath(output).exists) host.FS.unlink(output);
    await addFile(host, entry, encoder.encode(`
(def entry (dyn :current-file))
(import ./lib/trace)
(def report (trace/install (curenv) entry))
(def daw/script ${JSON.stringify(path)})
(dofile daw/script :env (curenv))
(spit ${JSON.stringify(output)} (json/encode (report)))
`));
    return entry;
}

export function readTrace(host) {
    return JSON.parse(host.FS.readFile(output, {encoding: "utf8"}));
}

// A short visual pulse also makes millisecond percussion notes visible between display frames.
export function activeLines(trace, path, seconds) {
    const lines = new Set();
    for (const [start, end, locations] of trace.events) {
        if (seconds < start || seconds >= Math.max(end, start + 0.08)) continue;
        for (const location of locations)
            for (const frame of trace.locations[location])
                if (frame.file.replace(/^\//, "") === path.replace(/^\//, "")) lines.add(frame.line);
    }
    return lines;
}
