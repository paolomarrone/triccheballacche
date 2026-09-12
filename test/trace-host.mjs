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

export {activeLines} from "../editor/trace.js";
