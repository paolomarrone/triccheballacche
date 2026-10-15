import {CodeJar} from "./vendor/codejar/codejar.js";
import {janetTokens} from "./janet.js";

export function codeEditor(element, {change, select, keydown}) {
    const numbers = document.getElementById("numbers"), marks = document.getElementById("marks");
    let jar, editable, readonly = true, value = "", cursor = 0, lineCount = 1, active = new Set(), painted = "";

    function highlight(node) {
        const source = node.textContent, tokens = [];
        let offset = 0;
        for (const [from, to, type] of janetTokens(source)) {
            const gap = source.slice(offset, from), text = source.slice(from, to);
            if (tokens.at(-1)?.[0] === type && /^\s*$/.test(gap)) tokens.at(-1)[1] += gap + text;
            else {
                if (gap) tokens.push([null, gap]);
                tokens.push([type, text]);
            }
            offset = to;
        }
        if (offset < source.length) tokens.push([null, source.slice(offset)]);

        // Keep unchanged text and tokens in place, including text already edited by the browser.
        const children = Array.from(node.childNodes);
        const same = (child, [type, text]) => child.textContent === text && (type ?
            child.nodeName === "SPAN" && child.className === `syntax-${type}` && child.childNodes.length === 1 &&
                child.firstChild.nodeType === Node.TEXT_NODE : child.nodeType === Node.TEXT_NODE);
        let first = 0, end = children.length, last = tokens.length;
        while (first < end && first < last && same(children[first], tokens[first])) ++first;
        while (end > first && last > first && same(children[end - 1], tokens[last - 1])) { --end; --last; }
        if (first === end && first === last) return;
        const fragment = document.createDocumentFragment();
        for (let i = first; i < last; ++i) {
            const [type, text] = tokens[i];
            if (!type) fragment.append(text);
            else {
                const token = document.createElement("span");
                token.className = `syntax-${type}`;
                token.textContent = text;
                fragment.append(token);
            }
        }
        const range = document.createRange();
        range.setStart(node, first);
        range.setEnd(node, end);
        range.deleteContents();
        range.insertNode(fragment);
    }

    function rows(parent, lines, numbered) {
        while (parent.children.length > lines.length) parent.lastChild.remove();
        lines.forEach((line, i) => {
            const row = parent.children[i] || parent.appendChild(document.createElement(numbered ? "span" : "div"));
            if (Number(row.dataset.line) !== line) {
                row.dataset.line = line;
                row.style.top = `calc(${line - 1} * var(--row))`;
                if (numbered) row.textContent = line;
            }
            if (numbered) row.classList.toggle("active", active.has(line));
        });
    }

    function paint() {
        const style = getComputedStyle(element), height = parseFloat(style.lineHeight);
        const top = element.scrollTop, bottom = top + element.clientHeight;
        const first = Math.max(0, Math.floor((top - parseFloat(style.paddingTop)) / height));
        const last = Math.min(lineCount, Math.ceil(bottom / height));
        const key = `${first}:${last}:${[...active].join(",")}`;
        if (key !== painted) {
            const visible = Array.from({length: Math.max(0, last - first)}, (_, i) => first + i + 1);
            rows(numbers, visible, true);
            rows(marks, visible.filter(line => active.has(line)), false);
            painted = key;
        }
        numbers.style.transform = marks.style.transform = `translateY(${-top}px)`;
    }

    function changed() {
        const next = jar.toString();
        if (next === value) return;
        value = next;
        lineCount = value.split("\n").length;
        paint();
        change();
    }

    function selection() {
        const s = window.getSelection();
        if (!s.focusNode || !element.contains(s.focusNode)) return;
        const range = document.createRange();
        range.selectNodeContents(element);
        range.setEnd(s.focusNode, s.focusOffset);
        const next = range.toString().length;
        if (next === cursor) return;
        cursor = next;
        select();
    }

    function readOnly(flag) {
        readonly = flag;
        const mode = flag ? "false" : editable;
        if (element.contentEditable !== mode) {
            element.contentEditable = mode;
            element.setAttribute("aria-readonly", String(flag));
        }
    }

    function load(source) {
        const focused = document.activeElement === element;
        if (focused) element.blur();
        jar?.destroy(); // A new document starts its own undo history.
        jar = CodeJar(element, highlight, {tab: "  ", autoclose: {open: "([{", close: ")]}"}});
        editable = element.contentEditable;
        element.style.whiteSpace = "pre";
        element.style.overflowWrap = "normal";
        readOnly(readonly);
        jar.onUpdate(changed);
        jar.updateCode(source, false);
        value = source;
        cursor = 0;
        lineCount = source.split("\n").length;
        element.scrollTop = element.scrollLeft = 0;
        if (focused) element.focus();
        paint();
    }

    // Let application shortcuts consume Enter before CodeJar handles indentation.
    const onKey = event => {
        keydown(event);
        if (readonly) event.stopImmediatePropagation();
    };
    const onClipboard = event => {
        if (readonly) { event.preventDefault(); event.stopImmediatePropagation(); }
    };
    element.addEventListener("keydown", onKey, true);
    element.addEventListener("paste", onClipboard, true);
    element.addEventListener("cut", onClipboard, true);
    element.addEventListener("input", changed);
    element.addEventListener("scroll", paint);
    document.addEventListener("selectionchange", selection);
    const resize = new ResizeObserver(paint);
    resize.observe(element);
    load("");

    return {
        get value() { return value; },
        load,
        readOnly,
        position() {
            const before = value.slice(0, cursor).split("\n");
            return `${before.length}:${before.at(-1).length + 1}`;
        },
        mark(lines) { active = new Set(lines); paint(); },
        reveal(line) {
            const rows = value.split("\n"), index = Math.max(0, Math.min(rows.length - 1, line - 1));
            const start = rows.slice(0, index).reduce((length, row) => length + row.length + 1, 0);
            element.focus();
            jar.restore({start, end: start + rows[index].length});
            element.scrollTop = Math.max(0, index * parseFloat(getComputedStyle(element).lineHeight) - element.clientHeight / 2);
            selection();
            paint();
        },
        destroy() {
            jar.destroy();
            resize.disconnect();
            element.removeEventListener("keydown", onKey, true);
            element.removeEventListener("paste", onClipboard, true);
            element.removeEventListener("cut", onClipboard, true);
            element.removeEventListener("input", changed);
            element.removeEventListener("scroll", paint);
            document.removeEventListener("selectionchange", selection);
        }
    };
}
