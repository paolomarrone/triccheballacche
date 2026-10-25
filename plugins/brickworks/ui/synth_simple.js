import {panel, el} from "./panel.js";

export async function create(element, callbacks) {
    const ui = panel(element, callbacks, "bw-simple");
    const modules = el("div", "bw-modules"); ui.root.append(modules);
    const oscillator = ui.group(modules, "Oscillator");
    for (const [id, label] of [["pulse_width", "Pulse width"], ["master_tune", "Tune"], ["portamento", "Glide"]])
        ui.control(oscillator, id, label);
    const filter = ui.group(modules, "Filter", "bw-filter");
    ui.control(filter, "cutoff", "Cutoff"); ui.control(filter, "resonance", "Resonance");
    const envelope = ui.group(modules, "Envelope", "bw-envelope");
    for (const id of ["attack", "decay", "sustain", "release"]) ui.control(envelope, id);
    const master = ui.group(modules, "Master", "bw-master");
    ui.control(master, "volume", "Volume"); ui.control(master, "level", "Output");
    await ui.ready;
    return ui;
}
