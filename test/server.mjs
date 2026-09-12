import {createServer} from "node:http";
import {readFile} from "node:fs/promises";
import {fileURLToPath} from "node:url";
import {resolve, extname, sep} from "node:path";

const root = fileURLToPath(new URL("../", import.meta.url));
const types = {".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript",
    ".css": "text/css", ".wasm": "application/wasm", ".json": "application/json"};

export function serve(port = 8000) {
    return createServer(async (request, response) => {
        response.setHeader("Cross-Origin-Opener-Policy", "same-origin");
        response.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
        try {
            const path = resolve(root, "." + decodeURIComponent(new URL(request.url, "http://localhost").pathname));
            if (!path.startsWith(resolve(root) + sep)) throw Error("Outside repository");
            const bytes = await readFile(path);
            response.setHeader("Content-Type", types[extname(path)] || "application/octet-stream");
            response.end(bytes);
        } catch {
            response.writeHead(404).end();
        }
    }).listen(port, "127.0.0.1");
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    serve();
    console.log("http://localhost:8000/editor/index.html");
}
