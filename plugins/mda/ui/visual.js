import {el, drag} from "./panel.js";

const svgNS = "http://www.w3.org/2000/svg";
function svg(tag, attributes = {}) {
    const node = document.createElementNS(svgNS, tag);
    for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, value);
    return node;
}
const clamp = v => Math.max(0, Math.min(1, v));
const db = v => 20 * Math.log10(Math.max(1e-6, v));

export function visual(ui, states, layout) {
    const {pad, figure} = layout;
    if (!pad && !figure) return;
    const frame = el("div", "mda-visual"); ui.root.append(frame);
    if (pad) {
        const [x, y] = pad.map(i => states[i]);
        const plane = el("div", "mda-plane"), point = el("button", "mda-point");
        point.type = "button"; point.textContent = "+";
        point.setAttribute("aria-label", `${x.p.name} / ${y.p.name}`);
        frame.append(el("span", "mda-axis mda-y", `${y.p.name} ↑`), plane, el("span", "mda-axis mda-x", `${x.p.name} →`));
        plane.append(point);
        const update = () => {
            point.style.left = `${x.ratio() * 100}%`; point.style.bottom = `${y.ratio() * 100}%`;
            plane.style.setProperty("--x", `${x.ratio() * 100}%`); plane.style.setProperty("--y", `${(1 - y.ratio()) * 100}%`);
            point.title = `${x.p.name} ${(x.value * 100).toFixed(1)}% · ${y.p.name} ${(y.value * 100).toFixed(1)}%`;
        };
        x.watch(update); y.watch(update);
        const move = e => {
            const r = plane.getBoundingClientRect();
            x.position((e.clientX - r.left) / r.width); y.position(1 - (e.clientY - r.top) / r.height);
        };
        drag(point, () => { x.begin(); y.begin(); }, move, () => { x.end(); y.end(); });
        point.onkeydown = e => {
            const change = {ArrowLeft: [x, -1], ArrowRight: [x, 1], ArrowUp: [y, 1], ArrowDown: [y, -1]}[e.key];
            if (!change) return;
            e.preventDefault(); e.stopPropagation();
            change[0].position(change[0].ratio() + change[1] * (e.shiftKey ? .001 : .01)); change[0].end();
        };
        return;
    }
    if (figure === "meter") {
        frame.classList.add("mda-spectrum");
        const chart = svg("svg", {viewBox: "0 0 260 104", role: "img", "aria-label": "Stereo octave levels"});
        frame.append(chart);
        for (let i = 0; i < 13; i++) {
            for (let ch = 0; ch < 2; ch++) {
                const state = states[5 + ch * 13 + i], bar = svg("rect", {x: i * 20 + ch * 7 + 3, width: 6, y: 90, height: 0, class: `channel-${ch}`});
                bar.dataset.parameter = state.p.id; chart.append(bar);
                state.watch(v => { const height = clamp((db(v) + 72) / 72) * 80; bar.setAttribute("height", height); bar.setAttribute("y", 90 - height); });
            }
            if (i % 2 === 0) {
                const text = svg("text", {x: i * 20 + 10, y: 102, "text-anchor": "middle"});
                const f = 7.8125 * 2 ** i; text.textContent = f >= 1000 ? `${f / 1000}k` : String(Math.round(f)); chart.append(text);
            }
        }
        const meters = el("div", "mda-levels"); frame.append(meters);
        for (const state of states.slice(0, 5)) {
            const row = el("div", "mda-level"), output = el("output", ""); row.dataset.parameter = state.p.id;
            row.append(el("span", "", state.p.name), output); meters.append(row);
            state.watch(v => { output.value = state.index === 4 ? v.toFixed(2) : `${db(v).toFixed(1)} dB`; });
        }
        return;
    }
    const chart = svg("svg", {viewBox: "0 0 280 100", role: "img"}); frame.append(chart);
    const path = svg("path", {class: "mda-curve"}), secondary = svg("path", {class: "mda-guide"});
    chart.append(secondary, path);
    const captions = {overdrive: "Transfer · before tone filter", steps: "Quantizer", bits: "Word length", pan: "Pan position",
        spiral: "Rate", fm: "Carrier / modulator", loop: "Loop capacity", drive: "Drive / bias"};
    const caption = el("span", "mda-caption", captions[figure]); frame.append(caption);
    chart.setAttribute("aria-label", captions[figure]);
    const plot = fn => Array.from({length: 129}, (_, i) => `${i ? "L" : "M"}${12 + i * 2},${50 - 38 * fn(i / 64 - 1)}`).join(" ");
    let pending = false, closed = false;
    const render = () => {
        pending = false;
        if (closed) return;
        const v = states.map(s => s.value);
        secondary.setAttribute("d", "M12 50H268 M140 8V92");
        if (figure === "overdrive") path.setAttribute("d", plot(x => (1 - v[0]) * x + v[0] * Math.sign(x) * Math.sqrt(Math.abs(x))));
        if (figure === "steps") path.setAttribute("d", plot(x => Math.trunc(x * (2 ** Math.floor(4 + 12 * v[1]))) / (2 ** Math.floor(4 + 12 * v[1]))));
        if (figure === "bits") {
            const n = 8 + 2 * Math.floor(8.9 * v[0]);
            path.setAttribute("d", Array.from({length: n}, (_, i) => `M${14 + i * 10} 75V25h6v50Z`).join(" "));
            caption.textContent = `${n} bit`;
        }
        if (figure === "pan") {
            const a = (v[0] - .5) * Math.PI, x = 140 + Math.sin(a) * 72, y = 84 - Math.cos(a) * 72;
            secondary.setAttribute("d", "M68 84A72 72 0 0 1 212 84");
            path.setAttribute("d", `M140 84L${x} ${y}m-4 0h8m-4 -4v8`);
        }
        if (figure === "spiral") path.setAttribute("d", Array.from({length: 180}, (_, i) => {
            const t = i / 16, r = 5 + i / 5, a = t * (v[1] >= .5 ? 1 : -1);
            return `${i ? "L" : "M"}${140 + r * Math.cos(a)},${50 + r * Math.sin(a)}`;
        }).join(" "));
        if (figure === "fm") {
            secondary.setAttribute("d", "M24 22H110V78H24Z M170 22H256V78H170Z M110 50H170m-9 -5l9 5l-9 5");
            path.setAttribute("d", "M32 50q16 -30 32 0t32 0 M178 50q16 -30 32 0t32 0");
        }
        if (figure === "loop") {
            const length = Math.floor(10.5 + 190 * v[0]);
            secondary.setAttribute("d", "M45 25H235a25 25 0 0 1 0 50H45a25 25 0 0 1 0 -50");
            path.setAttribute("d", `M45 25H${45 + 190 * v[0]}`);
            caption.textContent = `${length} s · ${v[2] > .5 ? "Record" : "Monitor"}`;
        }
        if (figure === "drive") {
            const x = 12 + 256 * v[1], y = 88 - 76 * v[2];
            path.setAttribute("d", `M${x - 7} ${y}h14M${x} ${y - 7}v14`);
        }
    };
    const update = () => { if (!pending) { pending = true; queueMicrotask(render); } };
    const inputs = {overdrive: [0], steps: [1], bits: [0], pan: [0], spiral: [1], loop: [0, 2], drive: [1, 2]};
    for (const index of inputs[figure] || []) states[index].watch(update);
    update();
    const free = ui.free;
    ui.free = () => { closed = true; free(); };
}
