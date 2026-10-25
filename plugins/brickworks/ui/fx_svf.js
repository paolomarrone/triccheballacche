import {panel} from "./panel.js";
import {plot} from "./plot.js";

export async function create(element, callbacks) {
    const ui = panel(element, callbacks, "bw-svf");
    plot(ui, ui.root, [{x: "cutoff", y: "q", label: "LP"}], "Q");
    const controls = ui.group(ui.root, "", "bw-hero");
    ui.control(controls, "cutoff", "Cutoff");
    ui.control(controls, "q", "Resonance");
    await ui.ready;
    return ui;
}
