import assert from "node:assert/strict";
import {execFileSync} from "node:child_process";
import {mkdtemp, mkdir, readFile, writeFile, rm, symlink} from "node:fs/promises";
import {resolve} from "node:path";
import {once} from "node:events";
import {nativeEditor} from "./native.mjs";
import {withBrowser} from "./chromium.mjs";
import {serve} from "./server.mjs";

const directory = await mkdtemp("build/test/settings-"), entry = `${directory}/score.janet`;
const config = resolve(directory, "config"), preferences = `${config}/triccheballacche/settings.json`;
const folder = resolve(directory, "plugins"), missing = `${directory}/missing`;
const initial = {perone: ["build/test/fixture.perone", folder], clap: [], lv2: [`${folder}/.lv2`], vst3: [`${folder}/.vst3`]};
const env = {XDG_CONFIG_HOME: config, CLAP_PATH: `${folder}:${folder}`};
const source = '(def p (daw/plugin "build/test/fixture.perone" {:gain 0.01})) (daw/output (daw/track p)) (daw/end (/ host/sample-rate 800))';
await mkdir(`${config}/triccheballacche`, {recursive: true});
await mkdir(folder);
await mkdir(`${folder}/.lv2`);
await mkdir(`${folder}/.vst3`);
await mkdir(`${folder}/objects.perone`); // Build artifacts without product.json are not bundles.
await symlink(resolve("build/test/fixture.clap"), `${folder}/fixture.clap`);
await writeFile(preferences, JSON.stringify({"plugin-paths": initial}));
await writeFile(entry, source);
const catalog = `${directory}/project.json`;
execFileSync("node", ["web/catalog.mjs", catalog, entry, "lib", "build/test/fixture.perone"]);
const server = serve(0);
let app;
try {
    await once(server, "listening");
    for (const mode of ["native", "web"]) {
        const url = mode === "native" ? await (app = nativeEditor(entry, env)).url :
            `http://127.0.0.1:${server.address().port}/editor/index.html?score=${entry}&project=../${catalog}`;
        await withBrowser(async ({call, evaluate, click, wait, set, diagnostics}) => {
            await call("Emulation.setDeviceMetricsOverride", {width: 1100, height: 760, deviceScaleFactor: 1, mobile: false});
            await call("Page.navigate", {url});
            await wait('!document.querySelector("#settings")?.disabled');
            // Direct backend probes share the bridge with the editor's status polling.
            if (mode === "native") await evaluate(`(() => {
                const call = webui.call.bind(webui); let queue = Promise.resolve();
                webui.call = (...args) => {
                    const result = queue.then(async () => {
                        const value = await call(...args);
                        if (window.holdCommand === args[1]) {
                            window.holdCommand = null;
                            await new Promise(resolve => { window.releaseCommand = resolve; });
                        }
                        return value;
                    });
                    queue = result.catch(() => {}); return result;
                };
            })()`);
            await evaluate(`import(${JSON.stringify(mode === "native" ? "./native.js" : "../web/editor.js")}).then(value => { window.backend = value; })`);
            await click("#settings");
            await click("#settings-audio-tab");
            assert(await evaluate('document.querySelector("#settings-audio-engine").disabled'));
            await wait('!document.querySelector("#audio-rate").disabled');
            assert.equal(await evaluate('document.querySelector("#audio-rate").value'), "48000");
            await set("#audio-rate", "44100");
            await wait('!document.querySelector("#audio-rate").disabled');
            assert.equal((await evaluate('backend.command("audio-info")')).sampleRate, 44100);
            await click('#settings-dialog [data-close]');
            await click("#play");
            await wait('Number(document.querySelector("#time").value) > 0');
            await click("#settings");
            assert(await evaluate('document.querySelector("#settings-audio-engine").checked'));
            assert(await evaluate('document.querySelector("#audio-rate").disabled'));
            assert.equal((await evaluate('backend.command("audio-info")')).sampleRate, 44100, "The player must use the selected rate");
            assert.equal((await evaluate('backend.command("score")')).score.end, Math.round(55.125 * 44100) / 44100, "Janet must compile at the selected rate");
            const rejected = await evaluate('backend.command("sample-rate", 96000)');
            assert.match(rejected.error, /engine off/);
            assert(rejected.audio && rejected.playing && rejected.prepared, "Rejected changes must preserve playback");
            if (mode === "native") {
                await evaluate('window.holdCommand = "status"');
                await wait('typeof window.releaseCommand === "function"');
            }
            await click("#settings-audio-engine");
            if (mode === "native") {
                await evaluate('window.holdCommand = "audio"; releaseCommand(); window.releaseCommand = null');
                await wait('typeof window.releaseCommand === "function"');
                assert(await evaluate('document.querySelector("#settings-audio-engine").disabled && !document.querySelector("#settings-audio-engine").checked'),
                    "A stale status must not undo a pending audio toggle");
                await evaluate('releaseCommand(); window.releaseCommand = null');
            }
            await wait('!document.querySelector("#audio").checked && document.querySelector("#state").textContent === "Stopped"');
            await click("#settings-audio-engine");
            await wait('document.querySelector("#audio").checked');
            assert.equal(await evaluate('document.querySelector("#state").textContent'), "Stopped");
            assert(await evaluate('document.querySelector("#audio-rate").disabled'), "Transport stop leaves the engine on");
            await click("#settings-audio-engine");
            await wait('!document.querySelector("#audio-rate").disabled');
            if (mode === "native") {
                await evaluate('window.holdCommand = "audio-info"');
                await click("#settings-audio-tab");
                await wait('typeof window.releaseCommand === "function"');
                assert(await evaluate('document.querySelector("#audio-rate").disabled && document.querySelector("#settings-audio-engine").disabled'),
                    "Audio controls must wait for the current state before accepting edits");
                await evaluate('releaseCommand(); window.releaseCommand = null');
                await wait('!document.querySelector("#audio-rate").disabled');
            }
            const unchanged = await evaluate('backend.command("sample-rate", 44100)');
            assert(unchanged.prepared && !unchanged.audio, "An unchanged rate retains the graph");
            for (const rate of [0, -1, 44100.5, 384001, "invalid"]) {
                const invalid = await evaluate(`backend.command("sample-rate", ${JSON.stringify(rate)})`);
                assert(invalid.error && invalid.prepared, "Invalid rates must leave the graph intact");
            }
            await set("#audio-rate", "96000");
            await wait('!document.querySelector("#audio-rate").disabled');
            assert.equal((await evaluate('backend.command("audio-info")')).sampleRate, 96000);
            assert(await evaluate('document.querySelector("#settings-audio-engine").disabled'), "A rate change invalidates the prepared graph");
            assert(!await evaluate('document.querySelector("#catalog-list").textContent.includes("in use")'));
            await click('#settings-dialog [data-close]');
            await click("#play");
            await wait('document.querySelector("#audio").checked && !document.querySelector("#play").disabled');
            assert.equal((await evaluate('backend.command("audio-info")')).sampleRate, 96000, "Changing rate must recreate the device");
            assert.equal((await evaluate('backend.command("score")')).score.end, 120, "Play must recompile unchanged source at the new rate");
            await click("#stop");
            await click("#settings");
            await click("#settings-audio-engine");
            await wait('!document.querySelector("#audio-rate").disabled');
            if (mode === "native") await evaluate('window.holdCommand = "sample-rate"');
            await set("#audio-rate", "48000");
            if (mode === "native") await wait('typeof window.releaseCommand === "function"');
            await click('#settings-dialog [data-close]');
            await click("#play");
            if (mode === "native") await evaluate('releaseCommand(); window.releaseCommand = null');
            await wait('document.querySelector("#audio").checked && !document.querySelector("#play").disabled');
            assert.equal((await evaluate('backend.command("audio-info")')).sampleRate, 48000, "Play must wait for a pending rate change");
            await click("#stop");
            await click("#settings");
            await writeFile(`build/test/settings-audio-${mode}.png`, Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            await click("#settings-plugins-tab");
            await wait('document.querySelectorAll(".catalog-item").length === 1');
            if (mode === "native") {
                await click('#settings-dialog [data-close]');
                const position = await evaluate('Number(document.querySelector("#time").value)');
                await click("#play");
                await wait(`Number(document.querySelector("#time").value) > ${position}`);
                await click("#settings");
                const revision = await evaluate('document.querySelector("#timeline").dataset.revision');
                assert.equal(await evaluate('document.querySelectorAll("#plugin-paths textarea").length'), 4);
                assert.equal(await evaluate('document.querySelector("#plugin-paths-clap").value'), "");
                await mkdir(`${folder}/broken.perone`);
                await writeFile(`${folder}/broken.perone/product.json`, "broken");
                await set("#plugin-paths-clap", `${folder}\n${folder}\n${missing}`);
                assert(!await evaluate('document.querySelector("#plugin-apply").disabled'));
                await click("#plugin-apply");
                await wait('document.querySelector("#settings-catalog").getAttribute("aria-busy") === "false"');
                assert.match(await evaluate('document.querySelector("#plugin-paths").textContent'), /Not found/);
                assert.deepEqual(JSON.parse(await readFile(preferences, "utf8"))["plugin-paths"].clap, [folder, missing]);
                assert(await evaluate('[...document.querySelectorAll(".catalog-item")].some(row => row.dataset.path.endsWith("fixture.clap"))'));
                assert(await evaluate('document.querySelector("#audio").checked'), "Scan must preserve the running engine");
                assert.equal(await evaluate('document.querySelector("#timeline").dataset.revision'), revision);
                assert.match(await evaluate('document.querySelector("#state").textContent'), /^Playing/);
                await set("#catalog-filter", "unavailable");
                assert.equal(await evaluate('document.querySelectorAll(".catalog-item").length'), 1);
                assert.match(await evaluate('document.querySelector("#catalog-list").textContent'), /broken.perone/);
                await set("#catalog-filter", "clap");
                assert(await evaluate('[...document.querySelectorAll(".catalog-item")].every(row => row.textContent.includes("CLAP"))'));
                await set("#catalog-search", "no matching plugin");
                assert.equal(await evaluate('document.querySelectorAll(".catalog-item").length'), 0);
                await set("#catalog-search", "");
                await set("#catalog-filter", "all");
                const saved = await readFile(preferences, "utf8");
                for (const invalid of ['{"clap":[42]}', '{"unknown":[]}', '[]', 'broken']) {
                    const result = await evaluate(`backend.command("library-save", ${JSON.stringify(invalid)})`);
                    assert(result.error, "Invalid settings must be rejected");
                    assert.equal(await readFile(preferences, "utf8"), saved);
                }
                await click("#plugin-defaults");
                assert(!await evaluate('document.querySelector("#plugin-apply").disabled'));
                const defaults = (await evaluate('document.querySelector("#plugin-paths-clap").value')).split("\n");
                assert.equal(defaults.filter(path => path === folder).length, 1, "Default paths must not contain duplicates");
                await click("#plugin-revert");
                assert(await evaluate('document.querySelector("#plugin-apply").disabled'));
                await click("#plugin-refresh");
                await wait('document.querySelector("#settings-catalog").getAttribute("aria-busy") === "false"');
            } else {
                assert(await evaluate('document.querySelector("#plugin-path-actions").hidden'));
                assert.equal(await evaluate('document.querySelectorAll("#plugin-paths textarea").length'), 0);
                assert.match(await evaluate('document.querySelector("#plugin-paths").textContent'), /Perone.*project.json/);
            }
            await writeFile(`build/test/settings-${mode}.png`, Buffer.from((await call("Page.captureScreenshot", {format: "png"})).data, "base64"));
            assert.deepEqual(diagnostics, []);
            await call("Page.navigate", {url: "about:blank"});
        });
        await app?.close(); app = undefined;
    }
    // The same persisted paths resolve IDs outside the GUI.
    const cli = `${directory}/clap.janet`;
    await writeFile(cli, '(def p (daw/plugin {:format :clap :id "org.triccheballacche.test.synth"})) (daw/output (daw/track p)) (daw/end 0.01)');
    execFileSync("./build/cli", [cli, `${directory}/test.wav`], {env: {...process.env, ...env}});
    app = nativeEditor(entry, env);
    await withBrowser(async ({call, evaluate, wait, click}) => {
        await call("Page.navigate", {url: await app.url});
        await wait('!document.querySelector("#settings")?.disabled');
        await click("#settings");
        await wait('document.querySelector("#plugin-paths-clap")');
        assert.equal(await evaluate('document.querySelector("#plugin-paths-clap").value'), `${folder}\n${missing}`);
        await writeFile(preferences, '{"plugin-paths":false}');
        await click("#plugin-refresh");
        await wait('!document.querySelector("#settings-error").hidden');
        assert.match(await evaluate('document.querySelector("#settings-error").textContent'), /Plugin paths must be an object/);
        for (const [format, paths] of Object.entries(initial)) await evaluate(`(() => {
            const input = document.querySelector('#plugin-paths-${format}');
            input.value = ${JSON.stringify(paths.join("\n"))}; input.dispatchEvent(new Event('input'));
        })()`);
        await click("#plugin-apply");
        await wait('document.querySelector("#settings-catalog").getAttribute("aria-busy") === "false" && document.querySelector("#settings-error").hidden');
        assert.deepEqual(JSON.parse(await readFile(preferences, "utf8"))["plugin-paths"], initial);
        // First save creates the configuration directory; failure keeps the draft available.
        await rm(config, {recursive: true});
        await writeFile(config, "Not a directory");
        await click("#plugin-defaults");
        await click("#plugin-apply");
        await wait('!document.querySelector("#settings-error").hidden');
        assert(!await evaluate('document.querySelector("#plugin-apply").disabled'));
        await click("#plugin-revert");
        await rm(config);
        await evaluate(`document.querySelector('#plugin-paths-clap').value = ${JSON.stringify(folder)};
            document.querySelector('#plugin-paths-clap').dispatchEvent(new Event('input'));`);
        await click("#plugin-apply");
        await wait('document.querySelector("#settings-catalog").getAttribute("aria-busy") === "false" && document.querySelector("#settings-error").hidden');
        assert.deepEqual(JSON.parse(await readFile(preferences, "utf8"))["plugin-paths"].clap, [folder]);
        await call("Page.navigate", {url: "about:blank"});
    });
    console.log("OK: shared settings, audio/sample rate lifecycle, native paths, scan, filtering, validation, persistence and CLI resolution");
} finally {
    await app?.close();
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    await rm(directory, {recursive: true, force: true});
}
