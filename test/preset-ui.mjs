import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";

// Exercise the actual shared panel and file envelope, with either transport backend.
export async function presets({evaluate, wait, click}, native, directory, node = 0) {
    const plugin = `.plugin[data-node="${node}"]`;
    const root = `document.querySelector('${plugin} .plugin-body > div')?.shadowRoot`;
    const input = `${root}?.querySelector('input')`;
    await evaluate(`document.querySelector('${plugin}').open = true`);
    await wait(`${input} && !document.querySelector('${plugin} .plugin-body > div').inert && !document.querySelector('${plugin} button[aria-label="Export preset"]').disabled`);
    const edit = async value => {
        await evaluate(`(() => { const input = ${input}; input.value = ${value};
            input.dispatchEvent(new Event('input')); input.dispatchEvent(new Event('change')); })()`);
        await wait(`Math.abs(Number(${input}?.value) - ${value}) < .00001`);
    };
    await edit(.625);
    await evaluate(`globalThis.presetView = ${root}.host`);
    if (!native) await evaluate(`(() => {
        const create = URL.createObjectURL;
        URL.createObjectURL = blob => { globalThis.exportedPreset = blob.text(); return create(blob); };
    })()`);
    await click(`${plugin} button[aria-label="Export preset"]`);
    await wait('document.querySelector("#play").getAttribute("aria-label") === "Play"');
    const path = `${directory}/roundtrip.tbpreset.json`;
    if (native) {
        await wait('document.querySelector(".preset-dialog")?.open');
        await evaluate(`(() => { const form = document.querySelector('.preset-dialog form');
            form.querySelector('input').value = ${JSON.stringify(path)}; form.requestSubmit(form.querySelector('button[value=save]')); })()`);
    }
    await wait(`${input} && !document.querySelector('${plugin} .plugin-body > div').inert && !document.querySelector('${plugin} button[aria-label="Export preset"]').disabled`);
    assert(await evaluate(`presetView === ${root}.host`), "Export preserves the open plugin view");
    const text = native ? await readFile(path, "utf8") : await evaluate('exportedPreset');
    const preset = JSON.parse(text);
    assert.equal(preset.version, 1); assert(preset.plugin && preset.data.length);
    await edit(.125);
    const upload = text => evaluate(`(() => {
        const input = document.querySelector('${plugin} input[type=file]'), files = new DataTransfer();
        files.items.add(new File([${JSON.stringify(text)}], 'test.tbpreset.json', {type: 'application/json'}));
        input.files = files.files; input.dispatchEvent(new Event('change'));
    })()`);
    await upload(JSON.stringify({...preset, plugin: "another-plugin"}));
    await wait('document.querySelector("#errors").textContent.includes("different plugin")');
    await wait(`!document.querySelector('${plugin} button[aria-label="Import preset"]').disabled`);
    await wait(`Math.abs(Number(${input}?.value) - .125) < .00001`);
    await upload(text);
    await wait(`Math.abs(Number(${input}?.value) - .625) < .00001`);
    assert.equal(await evaluate('document.querySelector("#errors").textContent'), "");
    return text;
}
