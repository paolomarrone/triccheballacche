import {spawn} from "node:child_process";

// Register exit and output handlers before waiting for readiness; cleanup also works after startup failure.
export function nativeEditor(entry) {
    const child = spawn("./build/gui", ["--serve", entry]);
    let output = "";
    const exited = new Promise(resolve => child.once("close", (code, signal) => resolve([code, signal])));
    const timeout = setTimeout(() => child.kill("SIGKILL"), 60000);
    exited.then(() => clearTimeout(timeout));
    child.stderr.on("data", data => output += data);
    const url = new Promise((resolve, reject) => {
        child.once("error", reject);
        exited.then(([code, signal]) => reject(Error(`Editor exited before readiness: ${code ?? signal}\n${output}`)));
        let text = "";
        child.stdout.on("data", data => {
            output += data;
            text += data;
            const match = text.match(/Editor: (http:\/\/\S+)\r?\n/);
            if (match) resolve(match[1]);
        });
    });
    return {
        url, exited,
        get log() { return output; },
        async close() {
            if (child.pid && child.exitCode === null && child.signalCode === null) child.kill();
            const result = await exited;
            if (result[0] !== 0 && result[1] !== "SIGTERM")
                throw Error(`Editor exited: ${result[0] ?? result[1]}\n${output}`);
            return result;
        }
    };
}
