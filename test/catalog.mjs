import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {mkdtemp, mkdir, readFile, writeFile, rm} from "node:fs/promises";
import {dirname, join, resolve} from "node:path";

const directory = await mkdtemp("build/test/catalog-");
const script = resolve("web/catalog.mjs");
const product = await readFile("test/perone/product.json");
const dsp = await readFile("build/test/fixture.perone/wasm32/fixture.wasm");
async function put(path, contents) {
    const target = join(directory, path);
    await mkdir(dirname(target), {recursive: true});
    await writeFile(target, contents);
}
try {
    await put("examples/score.janet", "(import ./sources/theme)\n");
    await put("examples/sources/theme.janet", "(def theme [60 64 67])\n");
    await put("examples/.hidden.janet", "(error :hidden)\n");
    for (const name of ["ready", "native-only"]) {
        await put(`plugins/${name}.perone/product.json`, product);
        await put(`plugins/${name}.perone/x86_64-linux/fixture.so`, "native");
        await put(`plugins/${name}.perone/ui/index.js`, "export default {};\n");
    }
    await put("plugins/ready.perone/wasm32/fixture.wasm", dsp);
    await put("plugins/ready.perone/wasm32/fixture-ui.wasm", new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0]));
    await put("plugins/ready.perone/ui/style.css", "body {}\n");
    const output = "published/project.json";
    execFileSync(process.execPath, [script, output, "examples", "plugins"], {cwd: directory});
    const files = JSON.parse(await readFile(join(directory, output), "utf8"));
    assert.deepEqual(files.map(file => file.path), [
        "examples/score.janet", "examples/sources/theme.janet",
        "plugins/ready.perone/product.json", "plugins/ready.perone/ui/index.js",
        "plugins/ready.perone/ui/style.css", "plugins/ready.perone/wasm32/fixture-ui.wasm",
        "plugins/ready.perone/wasm32/fixture.wasm"
    ]);
    for (const file of files) {
        assert.deepEqual(await readFile(join(directory, "published", file.url)), await readFile(join(directory, file.path)));
        assert.equal(!!file.asset, file.path.includes("/ui/") || file.path.endsWith("-ui.wasm"));
    }
    // Explicit bundle roots (including a trailing slash) obey the same readiness rule.
    execFileSync(process.execPath, [script, output, "plugins/native-only.perone/"], {cwd: directory});
    assert.deepEqual(JSON.parse(await readFile(join(directory, output), "utf8")), []);
    console.log("OK: web catalog keeps score imports and UI assets, and publishes only bundles with a Wasm DSP");
} finally {
    await rm(directory, {recursive: true, force: true});
}
