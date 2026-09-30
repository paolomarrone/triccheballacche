// Parameter controls adapted from tibia/templates/perone-web/ui.js (GPL-3.0-or-later).
import {parameterRatio, parameterValue, parameterText} from "./parameters.js";

let serial = 0;
export function create(element, callbacks) {
    const style = document.createElement("link");
    style.rel = "stylesheet"; style.href = new URL("./perone-ui.css", import.meta.url);
    const root = document.createElement("div");
    root.className = "perone-controls";
    const prefix = "perone-" + serial++ + "-", controls = [];
    const events = new AbortController();
    for (const [index, p] of callbacks.product.parameters.entries()) {
        const row = document.createElement("div"), name = document.createElement("label"), value = document.createElement("output");
        const field = document.createElement("div");
        row.className = "parameter"; field.className = "parameter-control";
        name.textContent = p.shortName || p.name || p.id;
        name.title = p.name || p.id;
        const choices = p.list && p.scalePoints ? Object.entries(p.scalePoints).sort((a, b) => a[1] - b[1]) : null;
        const output = p.direction == "output", toggle = !choices && (p.toggled || p.isBypass);
        const control = document.createElement(output ? "meter" : choices ? "button" : "input");
        control.id = prefix + index; name.htmlFor = control.id;
        field.append(control);
        let current = p.defaultValue, active = false;
        if (output) { control.min = p.minimum; control.max = p.maximum; }
        else if (choices) {
            field.append(choiceMenu(control, choices, name.textContent));
            value.hidden = true;
        } else if (toggle) { control.type = "checkbox"; control.setAttribute("role", "switch"); }
        else { control.type = "range"; control.min = 0; control.max = 1; control.step = "any"; }
        function set(next) {
            current = next;
            const ratio = parameterRatio(p, next), text = parameterText(p, next);
            if (toggle && !output) control.checked = next >= .5;
            else control.value = output || choices ? next : ratio;
            if (choices && !output) {
                control.textContent = text;
                control.setAttribute("aria-label", `${name.textContent}: ${control.textContent}`);
                for (const option of field.querySelectorAll('[role="option"]'))
                    option.setAttribute("aria-selected", Number(option.value) === next);
            }
            control.style.setProperty("--fill", `${ratio * 100}%`);
            value.textContent = text;
            if (!output && !choices && !toggle) control.setAttribute("aria-valuetext", value.textContent);
        }
        if (!output) {
            const begin = () => { if (!active) { active = true; callbacks.set_parameter_begin(index, current); } };
            const end = () => { if (active) { active = false; callbacks.set_parameter_end(index, current); } };
            if (!choices) {
                control.addEventListener("pointerdown", begin);
                control.addEventListener("keydown", event => { if (!event.repeat && event.key != "Tab") begin(); });
            }
            control.addEventListener("input", () => {
                let next = choices ? Number(control.value) : toggle ? (control.checked ? 1 : 0) : parameterValue(p, Number(control.value));
                begin();
                if (p.integer) next = Math.round(next);
                set(next); callbacks.set_parameter(index, next);
            });
            for (const event of ["change", "pointerup", "pointercancel", "keyup", "blur"]) control.addEventListener(event, end);
            controls[index] = { set, end };
        } else controls[index] = { set, end() {} };
        set(current);
        row.append(name, field, value); root.append(row);
    }
    for (const event of ["pointerup", "pointercancel"])
        root.ownerDocument.addEventListener(event, () => { for (const c of controls) c.end(); }, { signal: events.signal });
    const close = event => {
        const menu = root.querySelector(":popover-open");
        if (menu && !event.composedPath().includes(menu)) menu.hidePopover();
    };
    root.ownerDocument.addEventListener("scroll", close, {capture: true, signal: events.signal});
    root.ownerDocument.defaultView.addEventListener("resize", close, {signal: events.signal});
    element.append(style, root);
    return {
        set_parameter(index, value) { controls[index]?.set(value); },
        free() { events.abort(); for (const c of controls) c.end(); root.remove(); style.remove(); }
    };
}

function choiceMenu(control, choices, name) {
    const menu = document.createElement("div");
    menu.className = "parameter-menu"; menu.id = control.id + "-options"; menu.popover = "auto";
    menu.setAttribute("role", "listbox"); menu.setAttribute("aria-label", name);
    control.type = "button"; control.className = "parameter-choice";
    control.setAttribute("aria-haspopup", "listbox"); control.setAttribute("aria-controls", menu.id);
    control.setAttribute("popovertarget", menu.id);
    control.setAttribute("aria-expanded", "false");
    const close = () => { menu.hidePopover(); control.focus(); };
    for (const [label, number] of choices) {
        const option = document.createElement("button");
        option.type = "button"; option.textContent = label; option.value = number; option.tabIndex = -1;
        option.setAttribute("role", "option");
        option.onclick = () => {
            control.value = option.value;
            control.dispatchEvent(new Event("input")); control.dispatchEvent(new Event("change"));
            close();
        };
        menu.append(option);
    }
    menu.addEventListener("toggle", () => {
        const open = menu.matches(":popover-open");
        control.setAttribute("aria-expanded", open);
        if (!open) return;
        const r = control.getBoundingClientRect();
        menu.style.width = `${r.width}px`;
        search = "";
        const height = menu.getBoundingClientRect().height;
        menu.style.left = `${Math.max(4, Math.min(r.left, innerWidth - r.width - 4))}px`;
        menu.style.top = `${r.bottom + height + 8 <= innerHeight ? r.bottom + 4 : Math.max(4, r.top - height - 4)}px`;
        const selected = menu.querySelector('[aria-selected="true"]') || menu.firstElementChild;
        selected?.focus({preventScroll: true}); selected?.scrollIntoView({block: "nearest"});
    });
    control.addEventListener("keydown", event => {
        if (event.key === "ArrowDown" || event.key === "ArrowUp") { event.preventDefault(); menu.showPopover(); }
    });
    let search = "", stamp = 0;
    menu.onkeydown = event => {
        const options = [...menu.children], index = options.indexOf(menu.getRootNode().activeElement);
        let next;
        if (event.key === "Escape" || event.key === "Tab") {
            if (event.key === "Escape") { event.preventDefault(); event.stopPropagation(); }
            close(); return;
        }
        if (event.key === "Enter" || event.key === " ") { event.preventDefault(); options[index]?.click(); return; }
        if (event.key === "ArrowDown") next = options[(index + 1) % options.length];
        else if (event.key === "ArrowUp") next = options[(index + options.length - 1) % options.length];
        else if (event.key === "Home") next = options[0];
        else if (event.key === "End") next = options.at(-1);
        else if (event.key.length === 1 && event.key !== " " && !event.ctrlKey && !event.metaKey && !event.altKey) {
            search = (performance.now() - stamp < 600 ? search : "") + event.key.toLowerCase();
            stamp = performance.now();
            next = options.find(option => option.textContent.toLowerCase().startsWith(search));
        } else return;
        event.preventDefault(); next?.focus();
    };
    return menu;
}
