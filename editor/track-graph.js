import {signalGraph, graphPath, graphRows, routeRows} from "./graph.js";

// A compact routing map pinned to score lanes. Musical time stays in the score;
// this view only lays out the prepared graph and opens the existing inspector.
export function trackGraph(panel, select) {
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("aria-hidden", "true");
    let graph, rows, selected, cards = new Map(), cables = [];
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
        if (!rows || width <= 0 || height <= 0) return;
        const layout = routeRows(rows, graph, width, height);
        for (const node of layout.nodes.values()) {
            cards.get(node.id).style.cssText = `left:${node.x}px;top:${node.y - 10}px;width:${node.width}px`;
        }
        layout.edges.forEach((edge, i) => cables[i].path.setAttribute("d", "M" + edge.points.map(p => p.join(" ")).join("L")));
    }

    panel.onkeydown = event => {
        if (event.key === "Escape" && graph) { choose(undefined); event.stopPropagation(); }
    };

    return {
        resize,
        clear() { if (graph) choose(undefined); },
        score(score, preserve = false) {
            graph = signalGraph(score); rows = graphRows(score, graph.nodes);
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
