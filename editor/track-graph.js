import {graphPath, graphRows} from "./graph.js";

// A compact routing map pinned to score lanes. Musical time stays in the score;
// this view only lays out the prepared graph and opens the existing inspector.
export function trackGraph(panel, select) {
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("aria-hidden", "true");
    let graph, rows, selected, cards = new Map(), cables = [], rails = [];
    let width = 0, height = 0;

    function choose(id) {
        selected = id;
        const {up, down, ids} = graphPath(graph.nodes, id);
        for (const [key, card] of cards) {
            card.setAttribute("aria-pressed", key === id);
            card.classList.toggle("dim", id !== undefined && !up.has(key) && !down.has(key));
        }
        for (const {from, to, path} of cables) {
            const connected = up.has(from) && up.has(to) || down.has(from) && down.has(to);
            path.classList.toggle("highlighted", connected);
            path.classList.toggle("dim", id !== undefined && !connected);
            if (connected) svg.append(path);
        }
        return ids;
    }

    function resize(w, h) {
        width = w; height = h;
        if (!rows) return;
        // Keep controls outside the graph. Long chains use smaller blocks; the
        // divider lets the user trade score width for longer plugin names.
        const inset = 8, railWidth = Math.min(32, rails.length * 4);
        const available = Math.max(1, width - 70 - railWidth - inset);
        for (const node of rows.nodes.values()) {
            const count = rows.columns[node.track];
            node.gap = Math.min(12, available / count * .25);
            node.width = Math.min(96, available / count - node.gap);
            node.x = inset + node.column * (node.width + node.gap);
            node.y = node.track * height + (height + 18) / 2;
            cards.get(node.id).style.cssText = `left:${node.x}px;top:${node.y - 10}px;width:${node.width}px`;
        }
        for (const {from, to, slot, path} of cables) {
            const a = rows.nodes.get(from), b = rows.nodes.get(to);
            const inputs = graph.nodes.get(to).inputs.length;
            const x = a.x + a.width, y = b.y + (slot - (inputs - 1) / 2) * Math.min(3, 14 / inputs);
            let d = `M${x} ${a.y}`;
            if (a.track === b.track && b.column === a.column + 1) d += `L${b.x} ${y}`;
            else {
                // Leave below the blocks, then follow a shared rail to the input
                // lane. The arrival runs above its blocks, including at minimum height.
                d += `h${Math.min(3, a.gap / 3)}V${(a.track + 1) * height - 2}`;
                if (a.track !== b.track) {
                    const rail = width - 66 - rails.indexOf(to) * railWidth / Math.max(1, rails.length);
                    d += `H${rail}V${b.track * height + 20}`;
                }
                d += `H${b.x - Math.min(4, b.gap / 3)}V${y}H${b.x}`;
            }
            path.setAttribute("d", d);
        }
    }

    panel.onkeydown = event => {
        if (event.key === "Escape" && graph) { choose(undefined); event.stopPropagation(); }
    };

    return {
        resize,
        clear() { if (graph) choose(undefined); },
        score(score, layout, preserve = false) {
            graph = layout; rows = graphRows(score, graph.nodes);
            rails = [...new Set(graph.edges.filter(edge => rows.nodes.get(edge.from).track !== rows.nodes.get(edge.to).track).map(edge => edge.to))];
            if (!preserve || !graph.nodes.has(selected)) selected = undefined;
            cards.clear(); cables = [];
            svg.replaceChildren(); panel.replaceChildren(svg);
            for (const {id} of rows.nodes.values()) {
                const model = score.nodes[id], node = graph.nodes.get(id);
                const kind = model.product ? node.inputs.length ? "effect" : "instrument" : "mix";
                const card = document.createElement("button");
                card.className = `routing-node ${kind}`;
                card.dataset.node = id;
                card.textContent = model.label || model.name;
                card.title = `${card.textContent}\n${kind} · Click to open signal path`;
                card.setAttribute("aria-label", card.textContent);
                card.onclick = () => select(id, choose(id), rows.nodes.get(id).track);
                panel.append(card); cards.set(id, card);
            }
            for (const edge of graph.edges) {
                const path = document.createElementNS(svg.namespaceURI, "path");
                path.setAttribute("class", "routing-cable");
                path.dataset.from = edge.from; path.dataset.to = edge.to;
                svg.append(path); cables.push({...edge, path});
            }
            panel.dataset.revision = score.revision;
            resize(width, height); choose(selected);
        }
    };
}
