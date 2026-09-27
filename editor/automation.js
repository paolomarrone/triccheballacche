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
    const toggle = document.getElementById("show-automation"), menu = document.getElementById("automation-parameters");
    let score, choices = [], selected = [], data = [], hits = [];
    let visible = true, anchor;
    toggle.onclick = () => {
        visible = !visible;
        toggle.setAttribute("aria-pressed", visible);
        data = [];
        changed();
    };
    window.addEventListener("resize", () => menu.hidePopover());
    document.getElementById("roll").addEventListener("scroll", () => menu.hidePopover());

    function caption(button, index) {
        const names = choices[index].filter(p => selected[index].has(p.key)).map(p => p.name);
        button.textContent = `${names.length === 1 ? names[0] : `${names.length} parameters`} ▾`;
        button.title = names.join("\n") || "Choose automation parameters";
    }

    function checklist(button, index) {
        const list = choices[index], checked = selected[index];
        menu.replaceChildren();
        menu.setAttribute("aria-label", `Automation for track ${index + 1}`);
        const boxes = [];
        for (const p of [null, ...list]) {
            const label = document.createElement("label"), input = document.createElement("input");
            input.type = "checkbox";
            input.value = p?.key || "all";
            label.append(input);
            if (p) {
                const swatch = document.createElement("i");
                swatch.style.setProperty("--curve-hue", p.hue);
                label.append(swatch);
            }
            label.append(p?.name || "All");
            menu.append(label);
            boxes.push(input);
            input.onchange = () => {
                for (const item of p ? [p] : list) {
                    if (input.checked) checked.add(item.key);
                    else checked.delete(item.key);
                }
                update();
                changed();
            };
        }
        function update() {
            boxes[0].checked = checked.size === list.length;
            boxes[0].indeterminate = checked.size > 0 && checked.size < list.length;
            list.forEach((p, i) => boxes[i + 1].checked = checked.has(p.key));
            caption(button, index);
        }
        update();
        requestAnimationFrame(() => {
            if (!menu.matches(":popover-open")) return;
            const r = button.getBoundingClientRect(), width = menu.offsetWidth, height = menu.offsetHeight;
            menu.style.left = `${Math.max(8, Math.min(r.left, innerWidth - width - 8))}px`;
            menu.style.top = `${r.bottom + height + 4 <= innerHeight ? r.bottom + 4 : Math.max(8, r.top - height - 4)}px`;
        });
    }

    return {
        score(value, preserve = false) {
            menu.hidePopover();
            score = value;
            data = [];
            choices = score.tracks.map(track => trackNodes(score, track).flatMap(id => {
                const node = score.nodes[id], parameters = node.product?.parameters || mixer;
                return (node.automation || []).map(([index, count]) => {
                    const p = parameters[index];
                    return {key: `${id}:${index}`, node: id, parameter: index, count, p,
                        hue: (35 + id * 97 + index * 137.508) % 360,
                        name: `${p.name || p.id} · ${node.label || node.name}`};
                });
            }));
            selected = choices.map((list, i) => {
                const choice = list.reduce((best, p) => p.count > (best?.count || 0) ? p : best, null);
                return new Set(preserve ? list.filter(p => selected[i]?.has(p.key)).map(p => p.key) : choice ? [choice.key] : []);
            });
        },
        control(index) {
            const control = document.createElement("button");
            control.className = "track-automation";
            control.setAttribute("aria-label", `Automation for track ${index + 1}`);
            control.popoverTargetElement = menu;
            control.popoverTargetAction = "show";
            control.hidden = !choices[index].length;
            caption(control, index);
            control.onclick = event => {
                if (anchor === control && menu.matches(":popover-open")) {
                    event.preventDefault();
                    menu.hidePopover();
                    return;
                }
                anchor = control;
                checklist(control, index);
            };
            return control;
        },
        async query(view, revision, current) {
            const results = [];
            for (let i = view.first; i < Math.min(choices.length, view.first + view.count); ++i) {
                for (const p of choices[i]) {
                    if (!visible || !current()) return results;
                    if (selected[i].has(p.key)) {
                        const result = await request("automation", revision, p.node, p.parameter, String(view.from), String(view.to), view.bins);
                        results.push({index: i, key: p.key, result});
                    }
                }
            }
            return results;
        },
        accept(results) {
            data = results.filter(({index, key, result}) =>
                visible && !result.stale && result.revision === score.revision && selected[index].has(key));
        },
        draw(context, view, {label, scale, row, scroll, ruler, width, dark}) {
            hits = [];
            let curves = 0, dense = false;
            if (!visible) return {curves, dense};
            context.save();
            context.beginPath(); context.rect(label, ruler, width - label, context.canvas.clientHeight - ruler); context.clip();
            context.lineWidth = 1.25;
            for (const {index, key, result} of data) {
                if (index < view.first || index >= view.first + view.count || result.to <= view.from || result.from >= view.to) continue;
                const p = choices[index].find(p => p.key === key && selected[index].has(key));
                if (!p || !result.bins && !result.points?.length) continue;
                context.strokeStyle = `hsl(${p.hue} 65% ${dark ? 70 : 38}%)`;
                context.fillStyle = context.strokeStyle;
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
