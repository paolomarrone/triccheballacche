import {parameterRatio, parameterValue, parameterText} from "./parameters.js";

function readout(p, value) {
    if (p.unit === "hz" && Math.abs(value) >= 1000) { p = {...p, unit: "khz"}; value /= 1000; }
    return parameterText(p, Number(value.toPrecision(p.unit === "khz" ? 3 : 4)));
}

export function el(tag, className, text) {
    const node = document.createElement(tag);
    node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
}

// Pointer capture keeps a gesture alive outside the control, including on touch.
export function drag(node, begin, move, end) {
    let pointer;
    node.addEventListener("pointerdown", event => {
        if (event.button || pointer !== undefined) return;
        event.preventDefault(); node.focus({preventScroll: true});
        pointer = event.pointerId;
        node.setPointerCapture(pointer); begin(event);
    });
    node.addEventListener("pointermove", event => { if (event.pointerId === pointer) move(event); });
    const finish = event => {
        if (event.pointerId !== pointer) return;
        pointer = undefined; end();
    };
    for (const type of ["pointerup", "pointercancel", "lostpointercapture"]) node.addEventListener(type, finish);
}

export function panel(element, callbacks, kind, options = {}) {
    const prefix = options.prefix || "bw", format = options.format || readout;
    const style = el("link", "");
    style.rel = "stylesheet"; style.href = options.stylesheet || new URL("./panel.css", import.meta.url);
    const ready = new Promise((resolve, reject) => {
        style.onload = resolve; style.onerror = () => reject(Error("Could not load plugin stylesheet"));
    });
    const root = el("div", `${prefix}-panel ${kind}`);
    root.setAttribute("role", "group"); root.setAttribute("aria-label", callbacks.product.name);
    element.append(style, root);
    const parameters = callbacks.product.parameters.map((p, index) => {
        const views = [];
        return {
            p, index, value: p.defaultValue, active: false,
            ratio() { return parameterRatio(p, this.value); },
            set(value) {
                if (value === this.value) return;
                this.value = value; for (const view of views) view(value);
            },
            watch(view) { views.push(view); view(this.value); },
            begin() { if (!this.active) { this.active = true; callbacks.set_parameter_begin(index, this.value); } },
            end() { if (this.active) { this.active = false; callbacks.set_parameter_end(index, this.value); } },
            write(value) {
                if (!Number.isFinite(value) || p.direction === "output") return;
                value = p.integer || p.toggled ?
                    Math.max(Math.ceil(p.minimum), Math.min(Math.floor(p.maximum), Math.round(value))) :
                    Math.max(p.minimum, Math.min(p.maximum, value));
                this.begin(); this.set(value); callbacks.set_parameter(index, value);
            },
            position(ratio) { this.write(parameterValue(p, ratio)); }
        };
    });
    const byId = new Map(parameters.map(p => [p.p.id, p]));
    function parameter(id) {
        const p = byId.get(id);
        if (!p) throw Error(`Unknown parameter: ${id}`);
        return p;
    }
    function group(parent, title, className = "") {
        const section = el("section", `${prefix}-module ${className}`);
        if (title) section.append(el("h2", "", title));
        parent.append(section);
        const controls = el("div", `${prefix}-controls`); section.append(controls);
        return controls;
    }
    function control(parent, id, label, icons) {
        const state = parameter(id), p = state.p, row = el("div", `${prefix}-control`);
        row.dataset.parameter = id; row.title = p.name;
        const name = label ?? p.shortName ?? p.name;
        if (name && !p.toggled) row.append(el("span", `${prefix}-label`, name));
        parent.append(row);
        if (p.direction === "output") {
            row.classList.add(`${prefix}-output`);
            const meter = el("meter", ""), value = el("output", `${prefix}-value`);
            meter.min = p.minimum; meter.max = p.maximum;
            meter.setAttribute("aria-label", p.name);
            row.append(meter, value);
            state.watch(next => { meter.value = next; value.textContent = format(p, next); });
        } else if (p.toggled) {
            row.classList.add(`${prefix}-toggle`);
            const button = el("button", "", name); button.type = "button";
            button.setAttribute("aria-label", p.name);
            button.onclick = () => { state.write(state.value >= .5 ? 0 : 1); state.end(); };
            state.watch(next => button.setAttribute("aria-pressed", next >= .5));
            row.append(button);
        } else if (p.list) {
            row.classList.add(`${prefix}-discrete`);
            const choices = Object.entries(p.scalePoints).sort((a, b) => a[1] - b[1]);
            const buttons = el("span", `${prefix}-choices`); buttons.setAttribute("role", "group");
            buttons.setAttribute("aria-label", p.name);
            for (const [text, value] of choices) {
                const button = el("button", "", text); button.type = "button";
                if (icons?.[text]) {
                    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
                    const path = document.createElementNS(svg.namespaceURI, "path");
                    svg.setAttribute("viewBox", "0 0 24 16"); svg.setAttribute("aria-hidden", "true");
                    path.setAttribute("d", icons[text]); svg.append(path); button.replaceChildren(svg);
                    button.title = text; button.setAttribute("aria-label", text);
                }
                button.onclick = () => { state.write(value); state.end(); };
                state.watch(next => button.setAttribute("aria-pressed", (options.selected ? options.selected(p, next, value) : next === value)));
                buttons.append(button);
            }
            row.append(buttons);
        } else {
            const dial = el("span", `${prefix}-dial`), input = el("input", `${prefix}-range`), value = el("input", `${prefix}-value`);
            input.type = "range"; input.min = 0; input.max = 1; input.step = "any";
            input.setAttribute("aria-label", p.name);
            value.type = "text"; value.inputMode = "decimal"; value.spellcheck = false;
            value.setAttribute("aria-label", `${p.name} value`);
            dial.title = "Drag · Shift for fine adjustment · Double-click to reset";
            dial.append(input, el("span", `${prefix}-pointer`)); row.append(dial, value);
            let y, ratio;
            drag(input, event => { y = event.clientY; ratio = state.ratio(); state.begin(); }, event => {
                ratio = Math.max(0, Math.min(1, ratio + (y - event.clientY) / (event.shiftKey ? 1600 : 160)));
                y = event.clientY; state.position(ratio);
            }, () => state.end());
            input.ondblclick = () => { state.write(p.defaultValue); state.end(); };
            input.oninput = () => { state.position(Number(input.value)); input.value = state.ratio(); };
            input.onkeydown = event => {
                const direction = {ArrowLeft: -1, ArrowDown: -1, ArrowRight: 1, ArrowUp: 1, PageDown: -10, PageUp: 10}[event.key];
                if (!direction && event.key !== "Home" && event.key !== "End") return;
                event.preventDefault(); event.stopPropagation();
                if (event.key === "Home") state.write(p.minimum);
                else if (event.key === "End") state.write(p.maximum);
                else if (p.integer) state.write(state.value + direction);
                else state.position(state.ratio() + direction * (event.shiftKey ? .0005 : .005));
            };
            for (const type of ["change", "keyup", "blur"]) input.addEventListener(type, () => state.end());
            let cancel = false, edited = false;
            value.onfocus = () => { edited = false; value.value = String(Number(state.value.toPrecision(7))); value.select(); };
            value.oninput = () => { edited = true; };
            value.onkeydown = event => {
                event.stopPropagation();
                if (event.key !== "Escape" && event.key !== "Enter") return;
                event.preventDefault(); cancel = event.key === "Escape"; value.blur();
            };
            value.onblur = () => {
                if (edited && !cancel && value.value.trim()) { state.write(Number(value.value)); state.end(); }
                cancel = false; value.value = format(p, state.value);
            };
            state.watch(next => {
                const ratio = state.ratio();
                input.value = ratio; input.setAttribute("aria-valuetext", format(p, next));
                value.title = format(p, next);
                dial.style.setProperty("--ratio", ratio);
                dial.style.setProperty("--angle", `${ratio * 270}deg`);
                if (value.getRootNode().activeElement !== value) value.value = format(p, next);
            });
        }
        return row;
    }
    return {
        root, group, control, parameter, ready,
        set_parameter(index, value) { parameters[index]?.set(value); },
        free() { for (const p of parameters) p.end(); root.remove(); style.remove(); }
    };
}
