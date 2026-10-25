import {panel, el} from "./panel.js";
import {layouts} from "./layouts.js";
import {choices, modeIndex, readout} from "./controls.js";
import {visual} from "./visual.js";

export async function create(element, callbacks) {
    const {product} = callbacks, name = product.bundleName.slice(4), layout = layouts[name];
    if (!layout) throw Error(`Missing MDA layout: ${name}`);
    const presets = await fetch(new URL("./presets.json", import.meta.url)).then(r => {
        if (!r.ok) throw Error("Could not load MDA presets");
        return r.json();
    });
    const mode = (p, value) => {
        const index = product.parameters.indexOf(p);
        return Object.entries(choices[name]?.[index] || {})[modeIndex(name, index, value)];
    };
    const ui = panel(element, callbacks, `mda-${layout.skin} mda-${name}`, {prefix: "mda",
        stylesheet: new URL("./panel.css", import.meta.url),
        selected: (p, next, value) => mode(p, next)?.[1] === value,
        format: (p, v) => mode(p, v)?.[0] ?? (p.direction === "output" ? v.toFixed(2) : p.toggled ? (v >= .5 ? "On" : "Off") : readout(name, product.parameters.indexOf(p), v))});
    ui.root.style.setProperty("--accent", layout.color);
    const states = product.parameters.map(p => ui.parameter(p.id));
    if (presets.length > 1) {
        const select = el("select", "mda-presets");
        select.setAttribute("aria-label", "Factory preset");
        select.append(new Option("Presets", ""));
        for (const [i, preset] of presets.entries()) select.append(new Option(preset.name, String(i)));
        select.onchange = () => {
            if (select.value === "") return;
            for (const [i, value] of presets[Number(select.value)].values.entries()) { states[i].write(value); states[i].end(); }
            select.value = "";
        };
        ui.root.append(select);
    }
    visual(ui, states, layout);
    const modules = el("div", "mda-modules"); ui.root.append(modules);
    for (const [title, indices, kind = ""] of layout.groups) {
        const group = ui.group(modules, title, kind);
        group.style.setProperty("--columns", Math.min(indices.length, 4));
        for (const index of indices) {
            const p = states[index].p;
            let label = p.name.replace(/^(Envelope |Velocity to |Velocity |Hat |Kik |Snr |VCF |ENV |OSC |Gld )/, "").replace(/:$/, "");
            if (["Low", "Mid", "High"].includes(title)) label = label.replace(/^[LMH] /, "");
            label = label.replace("L <> M", "Low / Mid").replace("M <> H", "Mid / High");
            ui.control(group, p.id, label);
        }
    }
    try { await ui.ready; } catch (error) { ui.free(); throw error; }
    return ui;
}
