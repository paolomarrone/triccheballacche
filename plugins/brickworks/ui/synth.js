import {panel, el} from "./panel.js";

const waveforms = {
    Saw: "M2 13 12 3V13L22 3V13",
    Pulse: "M2 13V3H8V13H15V3H21V13H23",
    Triangle: "M1 13 6 3 12 13 18 3 23 13"
};

export async function create(element, callbacks) {
    const kind = callbacks.product.bundleName.endsWith("_poly") ? "bw-poly" : "bw-mono";
    const ui = panel(element, callbacks, `bw-synth ${kind}`);
    const modules = el("div", "bw-modules"); ui.root.append(modules);
    for (let i = 1; i <= 3; i++) {
        const group = ui.group(modules, `Oscillator ${i}`, "bw-oscillator");
        ui.control(group, `vco${i}_wave`, "", waveforms);
        for (const [id, label] of [["coarse", "Octave"], ["fine", "Tune"], ["level", "Level"], ["pw", "PW / Slope"]])
            ui.control(group, `vco${i}_${id}`, label);
        ui.control(group, `vco${i}_${i === 3 ? "kbd_ctrl" : "mod"}`, i === 3 ? "Keyboard" : "Mod");
    }
    const filter = ui.group(modules, "Filter", "bw-filter");
    for (const [id, label] of [["cutoff", "Cutoff"], ["resonance", "Resonance"], ["contour", "Contour"], ["mod", "Mod"], ["kbd_ctrl", "Keyboard tracking"]])
        ui.control(filter, `vcf_${id}`, label);
    for (const [id, name] of [["vcf", "Filter envelope"], ["vca", "Amp envelope"]]) {
        const group = ui.group(modules, name, "bw-envelope");
        for (const [stage, label] of [["attack", "Attack"], ["decay", "Decay"], ["sustain", "Sustain"], ["release", "Release"]])
            ui.control(group, `${id}_${stage}`, label);
    }
    const noise = ui.group(modules, "Noise / Mod", "bw-noise");
    ui.control(noise, "noise_color", "");
    ui.control(noise, "noise_level", "Noise");
    ui.control(noise, "mod_mix", "VCO 3 → Noise");
    const master = ui.group(modules, "Master", "bw-master");
    for (const [id, label] of [["portamento", "Glide"], ["master_tune", "Tune"], ["volume", "Volume"], ["a440", "A440"], ["level", "Output"]])
        ui.control(master, id, label);
    await ui.ready;
    return ui;
}
