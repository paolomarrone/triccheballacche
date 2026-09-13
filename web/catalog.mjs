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
    } else if (path.includes(".perone/ui/") || [".janet", ".json", ".wasm"].includes(extname(path))) files.add(path);
}
for (const root of roots) await collect(root);
const assets = basename(output, ".json") + "-files", manifest = [];
await mkdir(join(dirname(output), assets), {recursive: true});
for (const path of [...files].sort()) {
    // Keep each bundle's relative URLs intact, including ES imports, CSS and auxiliary UI Wasm.
    const bundle = path.match(/^(.*\.perone)\/(.*)$/);
    const hash = createHash("sha256").update(bundle ? bundle[1] : path).digest("hex").slice(0, 24);
    const name = bundle ? `${hash}/${bundle[2]}` : hash + extname(path);
    const url = `${assets}/${name}`;
    await mkdir(dirname(join(dirname(output), url)), {recursive: true});
    await copyFile(path, join(dirname(output), url));
    manifest.push({path, url, ...(bundle?.[2].startsWith("ui/") ? {asset: true} : {})});
}
await writeFile(output, JSON.stringify(manifest, null, 2) + "\n");
console.log(`${output}: ${manifest.length} files`);
