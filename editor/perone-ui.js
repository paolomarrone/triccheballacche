// Generic controls from tibia/templates/perone-web/ui.js (GPL-3.0-or-later).
let serial = 0;
export function create(element, callbacks) {
    const root = document.createElement("div");
    root.className = "perone-controls";
    const prefix = "perone-" + serial++ + "-", controls = [];
    const events = new AbortController();
    const format = new Intl.NumberFormat(undefined, { maximumFractionDigits: 3 });
    const units = { db: "dB", hz: "Hz", khz: "kHz", pc: "%" };
    for (const [index, p] of callbacks.product.parameters.entries()) {
        const row = document.createElement("label"), name = document.createElement("span"), value = document.createElement("output");
        name.textContent = p.name || p.id;
        const choices = p.list && p.scalePoints ? Object.entries(p.scalePoints) : null;
        const output = p.direction == "output", toggle = !choices && (p.toggled || p.isBypass);
        const control = document.createElement(output ? "meter" : choices ? "select" : "input");
        control.id = prefix + index; row.htmlFor = control.id;
        let current = p.defaultValue, active = false;
        const logarithmic = p.map == "logarithmic" && p.minimum * p.maximum > 0;
        const map = x => logarithmic ? p.minimum * Math.pow(p.maximum / p.minimum, x) : p.minimum + (p.maximum - p.minimum) * x;
        const unmap = x => logarithmic ? Math.log(x / p.minimum) / Math.log(p.maximum / p.minimum) : (x - p.minimum) / (p.maximum - p.minimum || 1);
        if (output) { control.min = p.minimum; control.max = p.maximum; }
        else if (choices) {
            for (const [label, number] of choices) {
                const option = document.createElement("option");
                option.value = number; option.textContent = label; control.append(option);
            }
        } else if (toggle) control.type = "checkbox";
        else { control.type = "range"; control.min = 0; control.max = 1; control.step = "any"; }
        function set(next) {
            current = next;
            if (toggle && !output) control.checked = next >= .5;
            else control.value = output || choices ? next : unmap(next);
            value.textContent = format.format(next) + (p.unit ? " " + (units[p.unit] || p.unit) : "");
            if (!output && !choices && !toggle) control.setAttribute("aria-valuetext", value.textContent);
        }
        if (!output) {
            const begin = () => { if (!active) { active = true; callbacks.set_parameter_begin(index, current); } };
            const end = () => { if (active) { active = false; callbacks.set_parameter_end(index, current); } };
            control.addEventListener("pointerdown", begin);
            control.addEventListener("keydown", event => { if (!event.repeat && event.key != "Tab") begin(); });
            control.addEventListener("input", () => {
                let next = choices ? Number(control.value) : toggle ? (control.checked ? 1 : 0) : map(Number(control.value));
                begin();
                if (p.integer) next = Math.round(next);
                set(next); callbacks.set_parameter(index, next);
            });
            for (const event of ["change", "pointerup", "pointercancel", "keyup", "blur"]) control.addEventListener(event, end);
            controls[index] = { set, end };
        } else controls[index] = { set, end() {} };
        set(current);
        row.append(name, control, value); root.append(row);
    }
    for (const event of ["pointerup", "pointercancel"])
        root.ownerDocument.addEventListener(event, () => { for (const c of controls) c.end(); }, { signal: events.signal });
    element.append(root);
    return {
        set_parameter(index, value) { controls[index]?.set(value); },
        free() { events.abort(); for (const c of controls) c.end(); root.remove(); }
    };
}
