import {trackNodes} from "./graph.js";

const mixer = [{name: "Gain", minimum: 0, maximum: 4}, {name: "Pan", minimum: -1, maximum: 1}];
const units = {db: "dB", hz: "Hz", khz: "kHz", pc: "%"};
const format = new Intl.NumberFormat(undefined, {maximumFractionDigits: 3});
const valueText = (p, value) => {
    const label = Object.entries(p.scalePoints || {}).find(([, number]) => number === value)?.[0];
    return label || format.format(value) + (p.unit ? ` ${units[p.unit] || p.unit}` : "");
};

export function automationRatio({minimum: min, maximum: max, map}, value) {
    value = Math.max(Math.min(min, max), Math.min(Math.max(min, max), value));
    const ratio = map === "logarithmic" && min * max > 0 && min !== max ?
        Math.log(value / min) / Math.log(max / min) : (value - min) / (max - min || 1);
    return Math.max(0, Math.min(1, ratio));
}

// Selection and one bounded window of immutable score automation, independent of DSP/UI readings.
export function automation(request, changed) {
    const toggle = document.getElementById("show-automation"), menu = document.getElementById("automation-parameters");
    let score, choices = [], selected = [], data = [];
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

    function visibleCurves(view) {
        if (!visible) return [];
        return data.flatMap(({index, key, result}) => {
            if (index < view.first || index >= view.first + view.count || result.to <= view.from || result.from >= view.to) return [];
            const parameter = choices[index].find(p => p.key === key && selected[index].has(key));
            return parameter && (result.bins || result.points?.length) ? [{index, parameter, result}] : [];
        });
    }

    return {
        curves: visibleCurves,
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
        hit(view, {row, ruler, scroll}, y, time) {
            const lane = Math.floor((y - ruler + scroll) / row);
            for (const {index, parameter: p, result} of visibleCurves(view).reverse()) {
                if (index !== lane || time < result.from || time >= result.to) continue;
                const point = result.points?.findLast(([t]) => t <= time);
                const bin = result.bins?.[Math.min(result.bins.length - 1, Math.floor((time - result.from) / (result.to - result.from) * result.bins.length))];
                if (!point && !bin) continue;
                const [low, high] = bin || [point[1], point[1]];
                const top = ruler + index * row - scroll;
                const a = top + row - 8 - automationRatio(p.p, low) * (row - 16);
                const b = top + row - 8 - automationRatio(p.p, high) * (row - 16);
                if (y < Math.min(a, b) - 4 || y > Math.max(a, b) + 4) continue;
                const value = bin ? `${valueText(p.p, low)}–${valueText(p.p, high)} · zoom in for individual changes` : valueText(p.p, low);
                return {node: p.node, order: point?.[2] ?? -1, text: `${p.name} · ${value} · ${time.toFixed(3)} s`};
            }
            return null;
        }
    };
}
