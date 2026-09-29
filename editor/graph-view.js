import {graphPath} from "./graph.js";

// Read-only routing: cards for plugins and explicit mixes, tracks as annotations.
// No transport queries, DSP state or musical time belong to this view.
export function graphView(panel, fit, select) {
    const world = document.createElement("div"), message = document.createElement("p");
    world.className = "graph-world"; message.className = "graph-message";
    message.textContent = "Press Play to see the graph";
    panel.append(world, message);
    panel.title = "Drag the background to pan · Wheel to zoom · Home to fit";
    let layout, selected, cards = new Map(), cables = [], drag;
    let x = 0, y = 0, scale = 1, width = 0, height = 0, needsFit = true;
    fit.disabled = true;

    function paint() {
        world.style.transform = `translate(${x}px, ${y}px) scale(${scale})`;
    }

    function home() {
        if (!layout || !panel.clientWidth || !panel.clientHeight) return;
        width = panel.clientWidth; height = panel.clientHeight;
        scale = Math.max(.001, Math.min(1, (width - 40) / layout.width, (height - 40) / layout.height));
        x = (width - layout.width * scale) / 2; y = (height - layout.height * scale) / 2;
        needsFit = false;
        paint();
    }
    fit.onclick = home;
    const observer = new ResizeObserver(() => {
        if (!panel.clientWidth || !panel.clientHeight) return;
        if (needsFit) home();
        else {
            x += (panel.clientWidth - width) / 2; y += (panel.clientHeight - height) / 2;
            width = panel.clientWidth; height = panel.clientHeight;
            paint();
        }
    });
    observer.observe(panel);
    window.addEventListener("pagehide", () => observer.disconnect());

    function zoom(factor, cx = width / 2, cy = height / 2) {
        const next = Math.max(.001, Math.min(2.5, scale * factor));
        x = cx - (cx - x) * next / scale; y = cy - (cy - y) * next / scale;
        scale = next;
        paint();
    }

    function choose(id) {
        selected = id;
        const {up, down, ids} = graphPath(layout?.nodes, id);
        cards.forEach((card, i) => {
            card.setAttribute("aria-pressed", i === id);
            card.classList.toggle("dim", id !== undefined && !up.has(i) && !down.has(i));
        });
        for (const {from, to, path} of cables) {
            const connected = up.has(from) && up.has(to) || down.has(from) && down.has(to);
            path.classList.toggle("highlighted", connected);
            path.classList.toggle("dim", id !== undefined && !connected);
        }
        return ids;
    }

    panel.onwheel = event => {
        event.preventDefault();
        const r = panel.getBoundingClientRect(), unit = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? height : 1;
        zoom(Math.exp(-event.deltaY * unit * .002), event.clientX - r.left, event.clientY - r.top);
    };
    panel.onpointerdown = event => {
        if (event.button !== 0 || event.target.closest("button")) return;
        panel.focus({preventScroll: true});
        drag = {x: event.clientX, y: event.clientY, moved: false};
        panel.setPointerCapture(event.pointerId);
    };
    panel.onpointermove = event => {
        if (!drag) return;
        const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
        if (!drag.moved && Math.hypot(dx, dy) < 3) return;
        drag = {x: event.clientX, y: event.clientY, moved: true};
        x += dx; y += dy; paint();
    };
    panel.onpointerup = () => {
        if (drag && !drag.moved) choose(undefined);
        drag = undefined;
    };
    panel.onpointercancel = () => { drag = undefined; };
    panel.onkeydown = event => {
        if (event.ctrlKey || event.metaKey || event.altKey) return;
        if (event.key === "Home") home();
        else if (event.key === "+" || event.key === "=") zoom(1.2);
        else if (event.key === "-") zoom(1 / 1.2);
        else if (event.key.startsWith("Arrow")) {
            x += event.key === "ArrowLeft" ? 40 : event.key === "ArrowRight" ? -40 : 0;
            y += event.key === "ArrowUp" ? 40 : event.key === "ArrowDown" ? -40 : 0;
            paint();
        } else if (event.key === "Escape" && selected !== undefined) choose(undefined);
        else return;
        event.preventDefault();
    };

    function svg(tag, attributes) {
        const element = document.createElementNS("http://www.w3.org/2000/svg", tag);
        for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
        return element;
    }

    return {
        score(score, graph, preserve = false) {
            layout = graph;
            if (!preserve || !layout.nodes.has(selected)) selected = undefined;
            cards.clear(); cables = [];
            const wires = svg("svg", {width: layout.width, height: layout.height, "aria-hidden": "true"});
            const defs = svg("defs", {}), arrow = svg("marker", {id: "graph-arrow", viewBox: "0 0 6 6",
                refX: 6, refY: 3, markerWidth: 6, markerHeight: 6, orient: "auto"});
            arrow.append(svg("path", {d: "M0 0L6 3L0 6Z"})); defs.append(arrow); wires.append(defs);
            world.replaceChildren(wires);
            world.style.width = `${layout.width}px`; world.style.height = `${layout.height}px`;
            const trackNames = new Map(score.tracks.map(([source, mixer]) =>
                [mixer, score.nodes[mixer].label || (source < 0 ? "Master" : "Track")]));
            const name = id => trackNames.get(id) || score.nodes[id].label || score.nodes[id].name;
            function route(id) {
                const stages = [name(id)];
                while (!layout.nodes.has(id)) {
                    id = score.nodes[id].inputs[0];
                    stages.unshift(name(id));
                }
                return stages.join(" → ");
            }
            for (const node of layout.nodes.values()) {
                const {id, inputs} = node, model = score.nodes[id];
                const card = document.createElement("button"), title = document.createElement("strong"), detail = document.createElement("small");
                const kind = model.product ? inputs.length ? "effect" : "instrument" : "mix";
                card.className = `graph-node ${kind}`; card.dataset.node = id;
                card.style.cssText = `left:${node.x}px;top:${node.y}px;width:${node.width}px;height:${node.height}px`;
                title.textContent = name(id);
                detail.textContent = [kind[0].toUpperCase() + kind.slice(1),
                    ...node.tracks.map(id => score.nodes[id].label), id === layout.output ? "Output" : ""].filter(Boolean).join(" · ");
                card.title = `${name(id)}\n${detail.textContent}\nInputs: ${model.inputs.map(route).join(", ") || "none"}`;
                if (node.tracks.length) card.title += `\nTracks: ${node.tracks.map(name).join(", ")}`;
                card.append(title, detail);
                card.onclick = () => select(id, choose(id));
                card.onfocus = () => {
                    panel.scrollTop = panel.scrollLeft = 0;
                    if (node.x * scale + x < 0 || (node.x + node.width) * scale + x > width ||
                        node.y * scale + y < 0 || (node.y + node.height) * scale + y > height) {
                        x = width / 2 - (node.x + node.width / 2) * scale;
                        y = height / 2 - (node.y + node.height / 2) * scale;
                        paint();
                    }
                };
                function port(top, output, label) {
                    const dot = document.createElement("i");
                    dot.className = output ? "graph-port output" : "graph-port";
                    dot.style.top = `${top}px`; dot.title = label;
                    card.append(dot);
                }
                for (const edge of node.incoming)
                    port(edge.points.at(-1).y - node.y, false, `From ${route(model.inputs[edge.slot])}`);
                port(node.height / 2, true, id === layout.output ? `${route(score.output)} → Audio output` : `To ${node.outputs.map(name).join(", ")}`);
                world.append(card); cards.set(id, card);
            }
            for (const {from, to, points} of layout.edges) {
                let d = `M${points[0].x} ${points[0].y}`;
                for (let i = 1; i < points.length; ++i) {
                    const a = points[i - 1], b = points[i], mid = (a.x + b.x) / 2;
                    d += `C${mid} ${a.y} ${mid} ${b.y} ${b.x} ${b.y}`;
                }
                const path = svg("path", {d, class: "graph-cable", "marker-end": "url(#graph-arrow)",
                    "data-from": from, "data-to": to});
                wires.append(path); cables.push({from, to, path});
            }
            message.hidden = true; fit.disabled = false;
            panel.dataset.revision = score.revision;
            choose(selected);
            needsFit ||= !preserve;
            if (needsFit) home();
        }
    };
}
