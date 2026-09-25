import assert from "node:assert/strict";
import {execFileSync, spawnSync} from "node:child_process";
import {existsSync, readFileSync, readdirSync, renameSync, rmSync, statSync, writeFileSync} from "node:fs";
import {join, resolve} from "node:path";

// Integration with real upstream sources and binaries, after `make library`.
// No network is permitted during the incremental/recovery checks.
const [source, root, platform, make = "make"] = process.argv.slice(2);
assert(source && root && platform && platform !== "wasm32", "Native library build required");
const names = readdirSync(join(source, "examples")).filter(name => /^(fx_|synth_)/.test(name)).sort();
assert(names.length > 0);
assert.deepEqual(readdirSync(root).filter(name => /^(fx|synth)/.test(name)).sort(), names);
const bundles = names.map(name => join(root, name, "build", `bw_example_${name}.perone`));
const binaries = bundles.map((path, i) => join(path, platform, `bw_example_${names[i]}.so`));
for (let i = 0; i < names.length; i++) {
    const {product} = JSON.parse(readFileSync(join(bundles[i], "product.json"), "utf8"));
    assert.equal(product.bundleName, `bw_example_${names[i]}`);
    assert(statSync(binaries[i]).size > 0);
}
execFileSync("./build/test/loader", bundles, {stdio: "inherit"});

const log = "build/test/library-build.log";
writeFileSync(log, "");
function rebuild(extra = [], success = true) {
    // MAKEFLAGS' jobserver descriptors are not inherited by Node child processes.
    // Explicit jobs avoid those stale descriptors while preserving variable overrides.
    const env = {...process.env};
    delete env.MAKEFLAGS;
    delete env.MFLAGS;
    const result = spawnSync(make, ["--no-print-directory", "-j2", "library", "GIT=false", "NPM=false",
        `LIBRARY_DEPS=${resolve(source, "..")}`, `BRICKWORKS_PERONE=${root}`, `PERONE_PLATFORM=${platform}`, ...extra],
        {encoding: "utf8", env});
    writeFileSync(log, (result.stdout || "") + (result.stderr || ""), {flag: "a"});
    if (result.error) throw result.error;
    if (success) assert.equal(result.status, 0, `Library rebuild failed; see ${log}`);
    else assert.notEqual(result.status, 0, "Compiler failure must reach the top-level make");
}
const mtimes = () => binaries.map(path => statSync(path, {bigint: true}).mtimeNs);
const before = mtimes();
rebuild(["CC=false"]);
assert.deepEqual(mtimes(), before, "A cached build must not recompile any binary");

// Simulate an interrupted build without discarding the working binary.
const missing = binaries[0], saved = missing + ".test-backup";
assert(!existsSync(saved));
renameSync(missing, saved);
try {
    rebuild(["CC=false"], false);
    rebuild();
    assert(existsSync(missing));
    assert.deepEqual(mtimes().slice(1), before.slice(1), "Repair must leave other binaries untouched");
} finally {
    if (existsSync(missing)) rmSync(saved);
    else renameSync(saved, missing);
}

// Exercise the same library discovery used by the native editor and an actual
// score using a polyphonic synth, compressor, stereo pan and reverb.
const env = {...process.env};
if (root === "build/library/brickworks") delete env.BRICKWORKS_PERONE;
else env.BRICKWORKS_PERONE = root;
writeFileSync("build/test/library-score.janet",
    '(spit "build/test/library-native.json" (eval-string (slurp "lib/library.janet")))\n' +
    '(dofile "examples/brickworks.janet")\n');
execFileSync("./build/cli", ["build/test/library-score.janet", "build/test/library-smoke.wav", "48000"],
    {stdio: "inherit", env});
const catalog = JSON.parse(readFileSync("build/test/library-native.json", "utf8"));
const found = new Set(catalog.files.map(file => file.path));
for (const bundle of bundles) assert(found.has(join(bundle, "product.json")), `Catalog missing ${bundle}`);
const wav = readFileSync("build/test/library-smoke.wav");
assert.equal(wav.toString("ascii", 0, 4), "RIFF");
assert(wav.length > 48000 * 4, "The score must produce a complete stereo render");
console.log(`OK: ${names.length} C bundles, DSP loading, offline cache, failure propagation, repair, native catalog and example render`);
