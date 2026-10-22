import {panel, el} from "./panel.js";
import {plot} from "./plot.js";

export async function create(element, callbacks) {
    const ui = panel(element, callbacks, "bw-eq");
    plot(ui, ui.root, [
        {x: "ls_cutoff", y: "ls_gain", label: "L", color: "bw-low"},
        {x: "peak_cutoff", y: "peak_gain", label: "M", color: "bw-mid"},
        {x: "hs_cutoff", y: "hs_gain", label: "H", color: "bw-high"}
    ], "dB");
    const modules = el("div", "bw-modules"); ui.root.append(modules);
    for (const [id, name, color, width] of [["ls", "Low shelf", "low", "q"], ["peak", "Peak", "mid", "bw"], ["hs", "High shelf", "high", "q"]]) {
        const group = ui.group(modules, name, `bw-${color}`);
        ui.control(group, `${id}_cutoff`, "Frequency");
        ui.control(group, `${id}_gain`, "Gain");
        ui.control(group, `${id}_${width}`, width === "q" ? "Q" : "Width");
    }
    await ui.ready;
    return ui;
}
