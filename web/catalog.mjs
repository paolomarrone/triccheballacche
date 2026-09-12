import {readdir, stat, mkdir, copyFile, writeFile} from "node:fs/promises";
import {dirname, basename, extname, join} from "node:path";
import {createHash} from "node:crypto";

// Explicit project roots, no parsing/evaluation of scores and no dependency list per example.
const [output, ...roots] = process.argv.slice(2);
if (!output || !roots.length) throw Error("Usage: node web/catalog.mjs output.json directory-or-file ...");
const files = new Set();
async function collect(path) {
    if ((await stat(path)).isDirectory()) {
        for (const entry of await readdir(path, {withFileTypes: true}))
            if (!entry.name.startsWith(".") && !entry.isSymbolicLink()) await collect(join(path, entry.name));
    } else if ([".janet", ".json", ".wasm"].includes(extname(path))) files.add(path);
}
for (const root of roots) await collect(root);
const assets = basename(output, ".json") + "-files", manifest = [];
await mkdir(join(dirname(output), assets), {recursive: true});
for (const path of [...files].sort()) {
    const name = createHash("sha256").update(path).digest("hex").slice(0, 24) + extname(path);
    const url = `${assets}/${name}`;
    await copyFile(path, join(dirname(output), url));
    manifest.push({path, url});
}
await writeFile(output, JSON.stringify(manifest, null, 2) + "\n");
console.log(`${output}: ${manifest.length} files`);
