import {trackNodes} from "./graph.js";

const mixer = [{name: "Gain", minimum: 0, maximum: 4}, {name: "Pan", minimum: -1, maximum: 1}];
const units = {db: "dB", hz: "Hz", khz: "kHz", pc: "%"};
const format = new Intl.NumberFormat(undefined, {maximumFractionDigits: 3});
const valueText = (p, value) => {
    const label = Object.entries(p.scalePoints || {}).find(([, number]) => number === value)?.[0];
    return label || format.format(value) + (p.unit ? ` ${units[p.unit] || p.unit}` : "");
};

// Selection and one bounded window of immutable score automation, independent of DSP/UI readings.
export function automation(request, changed) {
    let score, choices = [], selected = [], data = new Map(), hits = [];
    return {
        score(value, preserve = false) {
            score = value;
            data.clear();
            choices = score.tracks.map(track => trackNodes(score, track).flatMap(id => {
                const node = score.nodes[id], parameters = node.product?.parameters || mixer;
                return (node.automation || []).map(([index, count]) => {
                    const p = parameters[index];
                    return {key: `${id}:${index}`, node: id, parameter: index, count, p,
                        name: `${p.name || p.id} · ${node.label || node.name}`};
                });
            }));
            selected = choices.map((list, i) => {
                const choice = preserve ? list.find(p => p.key === selected[i]) :
                    list.reduce((best, p) => p.count > (best?.count || 0) ? p : best, null);
                return choice?.key || "";
            });
        },
        control(index) {
            const control = document.createElement("select");
            control.className = "track-automation";
            control.setAttribute("aria-label", `Automation for track ${index + 1}`);
            control.add(new Option("Automation off", ""));
            for (const p of choices[index]) control.add(new Option(p.name, p.key));
            control.hidden = !choices[index].length;
            control.value = selected[index];
            control.title = control.selectedOptions[0].text;
            control.onchange = () => {
                selected[index] = control.value;
                control.title = control.selectedOptions[0].text;
                data.delete(index);
                changed();
            };
            return control;
        },
        async query(view, revision) {
            const queries = [];
            for (let i = view.first; i < Math.min(choices.length, view.first + view.count); ++i) {
                const p = choices[i].find(p => p.key === selected[i]);
                if (p) queries.push(request("automation", revision, p.node, p.parameter, String(view.from), String(view.to), view.bins)
                    .then(result => ({index: i, key: p.key, result})));
            }
            return Promise.all(queries);
        },
        accept(results) {
            data.clear();
            for (const {index, key, result} of results)
                if (!result.stale && result.revision === score.revision && selected[index] === key) data.set(index, result);
        },
        draw(context, view, {label, scale, row, scroll, ruler, width, dark}) {
            hits = [];
            let curves = 0, dense = false;
            context.save();
            context.beginPath(); context.rect(label, ruler, width - label, context.canvas.clientHeight - ruler); context.clip();
            context.strokeStyle = dark ? "#edcf9b" : "#80551b";
            context.fillStyle = context.strokeStyle;
            context.lineWidth = 1.25;
            for (const [index, result] of data) {
                if (index < view.first || index >= view.first + view.count || result.to <= view.from || result.from >= view.to) continue;
                const p = choices[index].find(p => p.key === selected[index]);
                if (!p || !result.bins && !result.points?.length) continue;
                const top = ruler + index * row - scroll;
                const {minimum: min, maximum: max, map} = p.p;
                const y = value => {
                    const ratio = map === "logarithmic" && min * max > 0 && min !== max ?
                        Math.log(value / min) / Math.log(max / min) : (value - min) / (max - min || 1);
                    return top + row - 8 - Math.max(0, Math.min(1, ratio)) * (row - 16);
                };
                const x = t => Math.max(label, Math.min(width, label + (t - view.from) / scale));
                ++curves;
                context.beginPath();
                if (result.bins) {
                    dense = true;
                    const step = (result.to - result.from) / result.bins.length;
                    let previous = result.initial;
                    result.bins.forEach(([low, high, last], i) => {
                        const a = result.from + i * step, b = a + step;
                        if (b > view.from && a < view.to) {
                            context.globalAlpha = .17;
                            context.fillRect(x(a), y(high), Math.max(1, x(b) - x(a)), Math.max(1, y(low) - y(high)));
                            context.moveTo(x(a), y(previous)); context.lineTo(x(b), y(last));
                            hits.push({x: x(a), y: y(high) - 4, w: x(b) - x(a), h: y(low) - y(high) + 8,
                                node: p.node, order: -1, text: `${p.name} · ${valueText(p.p, low)}–${valueText(p.p, high)} · zoom in for individual changes`});
                        }
                        previous = last;
                    });
                } else {
                    result.points.forEach(([a, value, order], i) => {
                        const b = result.points[i + 1]?.[0] ?? result.to;
                        if (b <= view.from || a >= view.to) return;
                        context.moveTo(x(a), y(value)); context.lineTo(x(b), y(value));
                        if (result.points[i + 1] && b < view.to) context.lineTo(x(b), y(result.points[i + 1][1]));
                        hits.push({x: x(a), y: y(value) - 4, w: x(b) - x(a), h: 8,
                            node: p.node, order, text: `${p.name} · ${valueText(p.p, value)}`});
                    });
                }
                context.globalAlpha = .85;
                context.stroke();
            }
            context.restore();
            return {curves, dense};
        },
        hit(x, y, time) {
            const found = hits.findLast(h => x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h);
            return found ? {...found, text: `${found.text} · ${time.toFixed(3)} s`} : null;
        }
    };
}
