import assert from "node:assert/strict";
import {spawnSync} from "node:child_process";
import {mkdtempSync, mkdirSync, readdirSync, readFileSync, writeFileSync, symlinkSync, statSync, utimesSync, rmSync} from "node:fs";
import {resolve, join} from "node:path";

// Exercise the actual Makefile in an isolated build tree, without touching working binaries.
const [make = "make", cc = "cc", miniaudio = ".deps/miniaudio.h", emcc] = process.argv.slice(2);
const root = process.cwd(), directory = mkdtempSync("build/test/build-"), work = join(directory, "repo");
mkdirSync(work);
const env = {...process.env};
delete env.MAKEFLAGS;
delete env.MFLAGS;
delete env.MAKEOVERRIDES;
const native = ["build/obj/native/score.o", "build/obj/native/test/score_view.o", "build/test/score_view"];
const web = emcc ? ["build/obj/web-offline/score.o", "build/obj/web-player/score.o"] : [];
const targets = [...native, ...web];
const flags = {CC: cc, CFLAGS: "-O2 -Wall -Wextra", CPPFLAGS: "", LDFLAGS: ""};
if (emcc) flags.EMCC = emcc.includes("/") && !emcc.includes(" ") ? resolve(emcc) : emcc;
const times = paths => paths.map(path => statSync(join(work, path), {bigint: true}).mtimeNs);
function build(targets, overrides = {}, success = true) {
    const result = spawnSync(make, ["--no-print-directory", "-j4", ...targets,
        ...Object.entries({...flags, ...overrides}).map(([key, value]) => `${key}=${value}`)], {cwd: work, env, encoding: "utf8"});
    if (result.error) throw result.error;
    const output = (result.stdout || "") + (result.stderr || "");
    writeFileSync("build/test/build.log", output, {flag: "a"});
    if (success) assert.equal(result.status, 0, output);
    else assert.notEqual(result.status, 0, "Compiler failure was ignored");
}
try {
    writeFileSync("build/test/build.log", "");
    for (const name of readdirSync(root))
        if (/\.[ch]$/.test(name) || ["Makefile", "lib", "posix", "tools", "test", "plugins", "patches", ".deps"].includes(name))
            symlinkSync(join(root, name), join(work, name));
    // An unrelated sibling header must never override the pinned default.
    writeFileSync(join(directory, "miniaudio.h"), "#error Wrong miniaudio header\n");
    build(targets);
    assert(readFileSync(join(work, "build/config/native.compile"), "utf8").includes(".deps/miniaudio.h"));
    let before = times(targets);
    build([...targets].reverse());
    assert.deepEqual(times(targets), before, "An unchanged build or target order recompiled objects");

    flags.CFLAGS = "-O0 -g -Wall -Wextra -DNDEBUG";
    build(targets);
    assert(times(targets).every((time, i) => time > before[i]), "CFLAGS did not rebuild all variants");
    assert(readFileSync(join(work, native[1])).includes(Buffer.from("found->count < 400")), "NDEBUG disabled test assertions");
    assert(!spawnSync(resolve(work, native[2])).status, "Rebuilt projection test failed");
    before = times(targets);
    build(targets);
    assert.deepEqual(times(targets), before);

    flags.CPPFLAGS = "-DBUILD_CONFIGURATION_TEST=1";
    build(targets);
    assert(times(targets).every((time, i) => time > before[i]), "CPPFLAGS did not rebuild all variants");
    build(["build/obj/native/posix/assets.o"]); // Required GUI include paths survive user CPPFLAGS.
    before = times(targets);
    flags.LDFLAGS = "-g";
    build(targets);
    assert(times(native)[2] > before[2], "LDFLAGS did not relink");
    assert.deepEqual(times([native[0], native[1], ...web]), [before[0], before[1], ...before.slice(3)], "LDFLAGS recompiled objects");

    before = times(targets);
    flags.CC = "env " + cc;
    build(targets);
    assert(times(native).every((time, i) => time > before[i]), "CC did not rebuild native artifacts");
    assert.deepEqual(times(web), before.slice(3), "CC rebuilt Wasm objects");
    before = times(targets);
    build([native[0]], {CC: "false"}, false);
    assert.deepEqual(times(targets), before, "A failed compile replaced a working artifact");
    build(targets);
    assert(times(native).every((time, i) => time > before[i]), "Build did not recover after compiler failure");

    if (emcc) {
        before = times(targets);
        flags.EMCC = "env " + flags.EMCC;
        build(targets);
        assert.deepEqual(times(native), before.slice(0, 3), "EMCC rebuilt native artifacts");
        assert(times(web).every((time, i) => time > before[i + 3]), "EMCC did not rebuild Wasm objects");
    }

    const header = readFileSync(miniaudio, "utf8");
    for (const name of ["first", "second"]) {
        const source = join(directory, name, "miniaudio.h");
        mkdirSync(join(directory, name));
        writeFileSync(source, header + `\n/* ${name} explicit override */\n`);
        utimesSync(source, 1, 1);
        build(["build/generated/web/miniaudio.h"], {MINIAUDIO: resolve(source)});
        assert(readFileSync(join(work, "build/generated/web/miniaudio.h"), "utf8").includes(`${name} explicit override`),
            "An older dependency at a new path did not regenerate its patched copy");
    }
    console.log(`OK: ${emcc ? "native/Wasm" : "native"} incremental builds, flag/compiler changes, link-only changes, assertion preservation, failure recovery and dependency overrides`);
} finally {
    rmSync(directory, {recursive: true, force: true});
}
