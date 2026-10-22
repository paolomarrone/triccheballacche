import {panel, el} from "./panel.js";
import {plot} from "./plot.js";

const families = {
    "bw-dynamics": ["fx_comp", "fx_noise_gate"],
    "bw-drive": ["fx_bitcrush", "fx_cab", "fx_clip", "fx_dist", "fx_drive", "fx_fuzz", "fx_satur"],
    "bw-modulation": ["fx_chorus", "fx_flanger", "fx_phaser", "fx_ring_mod", "fx_trem", "fx_vibrato"],
    "bw-space": ["fx_comb", "fx_delay", "fx_reverb"],
    "bw-stereo": ["fx_balance", "fx_pan"]
};
const dynamics = [["", ["threshold", "ratio", "gain"]], ["Envelope", ["attack", "release", "ext_sidechain"]]];
const multimode = [["Filter", ["cutoff", "q"]], ["Mix", ["in", "lp", "bp", "hp"]]];
const groups = {
    fx_comp: dynamics, fx_noise_gate: dynamics,
    fx_iir1: multimode, fx_iir2: multimode, fx_mm1: multimode, fx_mm2: multimode,
    fx_comb: [["Feedforward", ["ff_delay", "ff"]], ["Feedback", ["fb_delay", "fb"]], ["", ["blend"]]],
    fx_reverb: [["", ["predelay", "decay", "wet"]], ["Tone", ["bandwidth", "damping"]]]
};
const labels = {
    sr_ratio: "Rate", bit_depth: "Bits", cutoff_low: "Low", cutoff_high: "High", type: "",
    ff_delay: "Delay", fb_delay: "Delay", ff: "Amount", fb: "Amount", blend: "Blend",
    in: "Input", lp: "Lowpass", bp: "Bandpass", hp: "Highpass", ext_sidechain: "Sidechain",
    rate: "Rate", amount: "Amount", center: "Center", damping: "Damping", decay: "Decay",
    max_rate: "Max rate", l_level: "L", r_level: "R"
};

export async function create(element, callbacks) {
    const product = callbacks.product, name = product.bundleName.replace(/^bw_example_/, "");
    const family = Object.entries(families).find(([, names]) => names.includes(name))?.[0] || "";
    const ui = panel(element, callbacks, `bw-effect ${family}${groups[name] ? "" : " bw-compact"}`);
    const remaining = new Map(product.parameters.filter(p => !p.hidden).map(p => [p.id, p]));
    if (remaining.has("cutoff") && remaining.has("q") && !groups[name])
        plot(ui, ui.root, [{x: "cutoff", y: "q", label: "F"}], "Q");
    const modules = el("div", "bw-modules"); ui.root.append(modules);
    function group(title, ids) {
        const params = ids.filter(id => remaining.has(id)).map(id => remaining.get(id));
        if (!params.length) return;
        const controls = ui.group(modules, title);
        const knobs = params.filter(p => p.direction !== "output" && !p.list && !p.toggled).length;
        controls.style.setProperty("--columns", Math.max(1, Math.min(4, knobs)));
        for (const p of params) { ui.control(controls, p.id, labels[p.id]); remaining.delete(p.id); }
    }
    for (const [title, ids] of groups[name] || []) group(title, ids);
    group("", [...remaining.keys()]);
    await ui.ready;
    return ui;
}
