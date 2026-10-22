import {el, drag} from "./panel.js";
import {parameterText} from "./parameters.js";

// A parameter plane, not a spectrum or an approximation of the DSP response.
export function plot(ui, parent, bands, ylabel) {
    const frame = el("div", "bw-plot"), area = el("div", "bw-plane");
    const x = ui.parameter(bands[0].x), y = ui.parameter(bands[0].y);
    frame.setAttribute("role", "group"); frame.setAttribute("aria-label", `Frequency / ${ylabel}`);
    frame.append(el("span", "bw-axis bw-y", ylabel), area);
    frame.append(el("span", "bw-axis bw-min", parameterText(x.p, x.p.minimum)),
        el("span", "bw-axis bw-max", parameterText(x.p, x.p.maximum)),
        el("span", "bw-axis bw-top", parameterText(y.p, y.p.maximum)),
        el("span", "bw-axis bw-bottom", parameterText(y.p, y.p.minimum)));
    if (y.p.minimum < 0) area.classList.add("bipolar");
    parent.append(frame);
    for (const band of bands) {
        const x = ui.parameter(band.x), y = ui.parameter(band.y);
        const point = el("button", `bw-point ${band.color || ""}`, band.label);
        point.type = "button";
        area.append(point);
        const render = () => {
            point.style.left = `${x.ratio() * 100}%`; point.style.bottom = `${y.ratio() * 100}%`;
            point.setAttribute("aria-label", `${band.label}: ${parameterText(x.p, x.value)}, ${parameterText(y.p, y.value)}`);
        };
        x.watch(render); y.watch(render);
        let px, py, rx, ry;
        drag(point, event => {
            px = event.clientX; py = event.clientY; rx = x.ratio(); ry = y.ratio();
            x.begin(); y.begin();
        }, event => {
            const r = area.getBoundingClientRect(), fine = event.shiftKey ? .1 : 1;
            rx = Math.max(0, Math.min(1, rx + fine * (event.clientX - px) / r.width));
            ry = Math.max(0, Math.min(1, ry - fine * (event.clientY - py) / r.height));
            px = event.clientX; py = event.clientY;
            x.position(rx); y.position(ry);
        }, () => { x.end(); y.end(); });
        point.onkeydown = event => {
            const amount = event.shiftKey ? .001 : .01;
            const state = event.key === "ArrowLeft" || event.key === "ArrowRight" ? x : y;
            if (!["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(event.key)) return;
            event.preventDefault(); event.stopPropagation();
            state.position(state.ratio() + (["ArrowLeft", "ArrowDown"].includes(event.key) ? -amount : amount));
        };
        for (const type of ["keyup", "blur"]) point.addEventListener(type, () => { x.end(); y.end(); });
        point.ondblclick = () => {
            x.write(x.p.defaultValue); y.write(y.p.defaultValue); x.end(); y.end();
        };
    }
}
