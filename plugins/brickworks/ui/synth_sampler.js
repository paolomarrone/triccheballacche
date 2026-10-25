import {panel} from "./panel.js";

export async function create(element, callbacks) {
    const ui = panel(element, callbacks, "bw-sampler");
    const controls = ui.group(ui.root, "", "bw-hero");
    ui.control(controls, "master_tune", "Tune");
    ui.control(controls, "volume", "Volume");
    ui.control(controls, "level", "Output");
    await ui.ready;
    return ui;
}
