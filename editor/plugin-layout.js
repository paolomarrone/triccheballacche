import {compact} from "./workspace.js";

let front = 0;

// Own only the frame. The caller owns the mounted UI and its lifetime.
export function floating(title, content, close) {
    const element = document.createElement("dialog"), header = document.createElement("header");
    const name = document.createElement("h2"), button = document.createElement("button");
    element.className = "plugin-window"; element.setAttribute("aria-label", title);
    name.textContent = title;
    button.className = "icon"; button.textContent = "×";
    button.title = "Close window"; button.setAttribute("aria-label", button.title);
    button.onclick = close;
    header.append(name, button); element.append(header, content);
    document.body.append(element);
    let x = 60 + (front % 6) * 24, y = x, drag;

    function position() {
        if (compact.matches) return;
        x = Math.max(0, Math.min(x, innerWidth - element.offsetWidth));
        y = Math.max(0, Math.min(y, innerHeight - element.offsetHeight));
        element.style.setProperty("--window-x", `${x}px`);
        element.style.setProperty("--window-y", `${y}px`);
    }
    function focus() {
        if (compact.matches) {
            element.close(); element.showModal();
        }
        element.style.zIndex = ++front;
        button.focus({preventScroll: true});
    }
    function layout() {
        drag = undefined;
        element.close();
        if (compact.matches) element.showModal();
        else element.show();
        position();
    }
    element.onpointerdown = () => { element.style.zIndex = ++front; };
    element.oncancel = event => { event.preventDefault(); close(); };
    element.onkeydown = event => {
        if (!event.defaultPrevented && event.key === "Escape") { event.preventDefault(); event.stopPropagation(); close(); }
    };
    header.onpointerdown = event => {
        if (compact.matches || event.button || event.target.closest("button")) return;
        drag = {x: event.clientX - x, y: event.clientY - y};
        header.setPointerCapture(event.pointerId);
        event.preventDefault();
    };
    header.onpointermove = event => {
        if (!drag) return;
        x = event.clientX - drag.x; y = event.clientY - drag.y;
        position();
    };
    header.onpointerup = header.onpointercancel = header.onlostpointercapture = () => { drag = undefined; };
    const observer = new ResizeObserver(position);
    observer.observe(element);
    window.addEventListener("resize", position);
    compact.addEventListener("change", layout);
    layout(); focus();
    return {element, focus, remove() {
        observer.disconnect();
        window.removeEventListener("resize", position);
        compact.removeEventListener("change", layout);
        element.close(); element.remove();
    }};
}
