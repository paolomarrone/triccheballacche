import assert from "node:assert/strict";
import {spawnSync} from "node:child_process";
import {cp, mkdtemp, mkdir, readFile, writeFile, rm} from "node:fs/promises";
import {resolve} from "node:path";
import {createHost, addFile, renderScore} from "../web/host.js";

const directory = await mkdtemp("build/test/plugin-references-");
const bundle = `${directory}/renamed 音.perone`, effect = "build/test/effect.perone";
const config = resolve(directory, "config"), preferences = `${config}/triccheballacche/settings.json`;
const entry = `${directory}/score.janet`, wave = `${directory}/score.wav`;
const env = {...process.env, XDG_CONFIG_HOME: config};
const log = [], host = await createHost({printErr: text => log.push(text)});
const text = value => new TextEncoder().encode(value);
const score = reference => `(def p (daw/plugin :keys ${reference} {:gain 0.25}))
(daw/output (daw/track p)) (daw/end 0.01)`;
const byId = '{:format :perone :id "fixture"}';
let referencePCM;

async function paths(roots) {
    await writeFile(preferences, JSON.stringify({"plugin-paths": {perone: roots}}));
    host.pluginPaths = {perone: roots};
}
async function run(source, error) {
    await writeFile(entry, source);
    await addFile(host, entry, text(source));
    log.length = 0;
    const result = spawnSync("./build/cli", [entry, wave, "48000"], {env, encoding: "utf8"});
    if (error) {
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, error);
        assert.throws(() => renderScore(host, entry, 48000));
        assert.match(log.join("\n"), error);
    } else {
        assert.equal(result.status, 0, result.stderr);
        const bytes = await readFile(wave);
        let native;
        for (let offset = 12; offset + 8 <= bytes.length;) {
            const size = bytes.readUInt32LE(offset + 4);
            if (bytes.toString("ascii", offset, offset + 4) === "data") {
                native = new Float32Array(bytes.buffer.slice(bytes.byteOffset + offset + 8, bytes.byteOffset + offset + 8 + size));
                break;
            }
            offset += 8 + size + (size & 1);
        }
        assert(native?.length);
        let wasm;
        try { wasm = renderScore(host, entry, 48000); }
        catch (error) { throw Error(log.join("\n"), {cause: error}); }
        referencePCM ||= native;
        assert.deepEqual(native, referencePCM);
        assert.deepEqual(wasm, referencePCM);
    }
    assert.equal(host.perone.instances.size, 0);
}

try {
    await addFile(host, "lib/pattern.janet", await readFile("lib/pattern.janet"));
    await mkdir(`${config}/triccheballacche`, {recursive: true});
    await cp("build/test/fixture.perone", bundle, {recursive: true});
    for (const root of [bundle, effect]) {
        for (const suffix of ["product.json", "wasm32/fixture.wasm"]) {
            await addFile(host, `${root}/${suffix}`, await readFile(`${root}/${suffix}`));
            await addFile(host, resolve(root, suffix), await readFile(`${root}/${suffix}`));
        }
    }
    await paths([`${directory}/missing`, bundle, effect]);
    for (const reference of [JSON.stringify(bundle), JSON.stringify(resolve(bundle)), byId,
        `{:format :perone :path ${JSON.stringify(bundle)}}`,
        `{:format :perone :path ${JSON.stringify(resolve(bundle))} :id "fixture"}`])
        await run(score(reference));
    for (const [reference, error] of [
        ['{:format :perone :id "missing"}', /plugin not found: missing/],
        [`{:format :perone :path ${JSON.stringify(bundle)} :id "missing"}`, /plugin ID not found/],
        ['{:format :perone}', /plugin reference needs :id or :path/],
        [`{:format :perone :path ${JSON.stringify(bundle)} :id false}`, /plugin :id must be a nonempty string/],
        ['{:format :perone :id ""}', /plugin :id must be a nonempty string/],
        ['{:format :perone :id "fixture" :path false}', /plugin :path must be a string/],
        ['{:format :perone :id 42}', /plugin :id must be a nonempty string/]])
        await run(score(reference), error);
    // Search-path order resolves duplicate IDs; an explicit path always wins.
    await paths([effect, bundle]);
    await run(`(assert (= (get-in (plugin/read ${byId}) [:layout :input-channels]) 1))\n` + score(JSON.stringify(bundle)));
    await paths([]);
    await run(score(byId), /plugin not found: fixture/);
    await run(score(`{:format :perone :path ${JSON.stringify(bundle)}}`));
    console.log("OK: Perone IDs, relocated bundles, explicit paths, precedence and disabled discovery; native/Wasm PCM is identical");
} finally {
    await rm(directory, {recursive: true, force: true});
}
