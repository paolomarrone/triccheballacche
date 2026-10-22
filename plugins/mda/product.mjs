import {readFile, writeFile} from "node:fs/promises";
import {choices, toggles} from "./ui/controls.js";
const [name, raw, output] = process.argv.slice(2);
const info = JSON.parse(await readFile(raw, "utf8"));
const slug = name.replace(/^mda/, "").replace(/!/g, "").toLowerCase();
const parameters = info.parameters.map((p, i) => ({
    id: p.name.trim().toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_|_$/g, "") || `parameter_${i}`,
    name: p.name.trim(), minimum: 0, maximum: 1, defaultValue: p.defaultValue, direction: "input", unit: "coefficient"
}));
if (slug === "specmeter") {
    parameters.length = 0;
    for (const [id, label] of [["peak_l", "Peak L"], ["peak_r", "Peak R"], ["rms_l", "RMS L"], ["rms_r", "RMS R"], ["correlation", "Correlation"],
        ...["l", "r"].flatMap(c => Array.from({length: 13}, (_, i) => [`band_${c}_${i}`, `${c.toUpperCase()} ${Math.round(7.8125 * 2 ** i)} Hz`]))]) {
        parameters.push({id, name: label, minimum: 0, maximum: id === "correlation" ? 1 : 16, defaultValue: 0, direction: "output"});
    }
}
for (const [index, scalePoints] of Object.entries(choices[slug] || {})) Object.assign(parameters[index], {list: true, scalePoints});
for (const index of toggles[slug] || []) {
    parameters[index].toggled = true;
    parameters[index].defaultValue = parameters[index].defaultValue > .5 ? 1 : 0;
    for (const preset of info.presets) preset.values[index] = preset.values[index] > .5 ? 1 : 0;
}
const ids = new Set();
for (const [i, p] of parameters.entries()) {
    if (ids.has(p.id)) p.id += `_${i + 1}`;
    ids.add(p.id);
}
const buses = [];
if (info.inputs) buses.push({id: "input", name: "Input", type: "audio", direction: "input", channels: "stereo"});
buses.push({id: "output", name: "Output", type: "audio", direction: "output", channels: "stereo"});
if (info.midi) buses.push({id: "midi", name: "MIDI", type: "midi", direction: "input"});
const product = {name: `MDA ${name.slice(3)}`, version: "1.0.0", buildVersion: "1", bundleName: `mda_${slug}`,
    buses, parameters, ui: {web: "ui/main.js"}};
await writeFile(output, JSON.stringify({company: {name: "MDA", url: "https://github.com/pluginguru/mda-effects"}, product}, null, 2) + "\n");
await writeFile(output.replace(/product.json$/, "presets.json"), JSON.stringify(slug === "specmeter" ? [] : info.presets) + "\n");
