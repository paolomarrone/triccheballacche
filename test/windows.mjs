import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {copyFile, mkdtemp, readFile, writeFile, rm} from "node:fs/promises";
import {resolve} from "node:path";
import {nativeEditor} from "./native.mjs";
import {withBrowser} from "./chromium.mjs";

assert.equal(process.platform, "win32");
const directory = await mkdtemp("build/test/windows caffè-音楽-");
const plugin = `${directory}/test synth.clap`, entry = `${directory}/score.janet`;
let app;
try {
    await copyFile("build/test/fixture.clap", plugin);
    await writeFile(entry, (await readFile("examples/patterns.janet", "utf8")).replaceAll("../lib/", "../../../lib/"));
    const scanned = JSON.parse(execFileSync("./build/tools/plugin-scan.exe", [plugin], {encoding: "utf8"}));
    assert.equal(scanned.length, 2);
    assert(scanned.every(p => !p.error));
    execFileSync("./build/cli.exe", [entry, `${directory}/render.wav`, "48000"], {stdio: "inherit"});
    app = nativeEditor(entry, {APPDATA: resolve(directory)});
    const url = await app.url;
    await withBrowser(async ({call, evaluate, wait, click, diagnostics}) => {
        await call("Page.navigate", {url});
        await wait('document.querySelector("#play")?.disabled === false');
        const command = (op, ...args) => evaluate(`webui.call("command", ${[op, ...args].map(JSON.stringify).join(",")}).then(JSON.parse)`);
        const catalog = await command("library");
        assert(!catalog.error, JSON.stringify(catalog));
        assert(catalog.plugins.filter(p => p.format === "perone" && !p.error).length >= 40);
        assert.equal(catalog.settingsPath, `${resolve(directory).replaceAll("\\", "/")}/triccheballacche/settings.json`);
        const paths = {perone: ["build/library/brickworks"], clap: [plugin], lv2: [], vst3: []};
        const preferences = await command("library-save", JSON.stringify(paths));
        assert(!preferences.error && !preferences.settingsError, JSON.stringify(preferences));
        assert(preferences.plugins.some(p => p.format === "clap" && !p.error));
        assert.deepEqual(JSON.parse(await readFile(preferences.settingsPath, "utf8"))["plugin-paths"], paths);
        const reloaded = await command("library");
        assert(!reloaded.error && !reloaded.settingsError, JSON.stringify(reloaded));
        assert.deepEqual(reloaded.sources.find(s => s.format === "clap").paths, [plugin]);
        const listing = await command("files", directory);
        assert(!listing.error && /^[A-Z]:\//i.test(listing.path), JSON.stringify(listing));
        const path = `${directory}/score caffè-音楽 with spaces.janet`;
        for (const text of ["# first version\n", "# replacement\n"]) {
            const saved = await command("save", path, text);
            assert(!saved.error, JSON.stringify(saved));
            assert.equal(await readFile(path, "utf8"), text);
            const opened = await command("open", path);
            assert(!opened.error && opened.text === text, JSON.stringify(opened));
        }
        await click("#apply");
        await wait('document.querySelector("#state").textContent === "Ready" && !document.querySelector("#apply").disabled');
        const prepared = await command("status");
        assert(prepared.state.prepared && prepared.audio, JSON.stringify(prepared));
        await click("#play");
        await wait('document.querySelector("#play").getAttribute("aria-label") === "Stop"');
        await click('#play[aria-label="Stop"]');
        await click("#settings");
        await click("#settings-audio-tab");
        await click("#settings-audio-engine");
        await wait('document.querySelector("#state").textContent === "Audio off"');
        assert.equal(diagnostics.length, 0, JSON.stringify(diagnostics));
        console.log("OK: Windows UTF-8 arguments, scanner, CLI, settings, editor files and audio preparation/play/stop");
        await command("close");
    });
} finally {
    await app?.close();
    await rm(directory, {recursive: true, force: true});
}
