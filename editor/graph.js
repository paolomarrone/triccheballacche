import {Graph, layout} from "./vendor/dagre/dagre.esm.js";

// Nodes owned by this track, in signal order. Other track mixers are boundaries.
export function trackNodes(score, track) {
    const nodes = [], seen = new Set();
    const boundaries = new Set(score.tracks.filter(t => t !== track).map(t => t[1]));
    function visit(id) {
        if (seen.has(id) || boundaries.has(id)) return;
        seen.add(id);
        for (const input of score.nodes[id].inputs) visit(input);
        nodes.push(id);
    }
    visit(track[1]);
    return nodes;
}

// Ancestors and descendants of one node, in signal order. Following a signal
// through a merge must not pull in the other inputs of that downstream merge.
export function graphPath(nodes, id) {
    const up = new Set(), down = new Set(), seen = new Set(), ids = [];
    if (id === undefined) return {up, down, ids};
    function visit(id, direction, found) {
        if (found.has(id)) return;
        found.add(id);
        for (const next of nodes.get(id)[direction]) visit(next, direction, found);
    }
    visit(id, "inputs", up); visit(id, "outputs", down);
    function order(id) {
        if (seen.has(id) || (!up.has(id) && !down.has(id))) return;
        seen.add(id);
        for (const input of nodes.get(id).inputs) order(input);
        ids.push(id);
    }
    for (const id of nodes.keys()) order(id);
    return {up, down, ids};
}

// Pin nodes to the existing score lanes. Shared nodes have one owner; columns
// follow the signal and leave room for parallel nodes within the same lane.
export function graphRows(score, graph) {
    const owners = new Map(), nodes = new Map(), next = [];
    score.tracks.forEach((track, index) => {
        for (const id of trackNodes(score, track))
            if (graph.has(id) && !owners.has(id)) owners.set(id, index);
    });
    function visit(id) {
        if (nodes.has(id)) return nodes.get(id);
        const inputs = graph.get(id).inputs.map(visit);
        const track = owners.get(id) ?? Math.max(0, score.tracks.length - 1);
        const column = Math.max(next[track] || 0, ...inputs.filter(node => node.track === track).map(node => node.column + 1));
        const node = {id, track, column};
        next[track] = column + 1;
        nodes.set(id, node);
        return node;
    }
    for (const id of graph.keys()) visit(id);
    const indents = [];
    for (const node of nodes.values()) if (!node.column)
        indents[node.track] = !(score.nodes[node.id].product && !graph.get(node.id).inputs.length);
    return {nodes, columns: next, indents};
}

// Route within fixed score lanes. Interval partitioning reuses a vertical rail
// only after its previous cable ends; fan-in/out also gets separate ports.
export function routeRows(rows, graph, width, row) {
    const nodes = new Map([...rows.nodes].map(([id, node]) => [id, {...node, incoming: [], outgoing: []}]));
    const edges = graph.edges.map(({from, to, slot}) => {
        const a = nodes.get(from), b = nodes.get(to);
        const edge = {from, to, slot, direct: a.track === b.track && b.column === a.column + 1};
        a.outgoing.push(edge); b.incoming.push(edge);
        return edge;
    });
    const cross = edges.filter(e => nodes.get(e.from).track !== nodes.get(e.to).track);
    const bounds = e => [Math.min(nodes.get(e.from).track, nodes.get(e.to).track),
        Math.max(nodes.get(e.from).track, nodes.get(e.to).track)];
    cross.sort((a, b) => bounds(a)[0] - bounds(b)[0] || bounds(b)[1] - bounds(a)[1] || a.from - b.from || a.slot - b.slot);
    const ends = [];
    for (const edge of cross) {
        const [start, end] = bounds(edge);
        let rail = ends.findIndex(last => last < start);
        if (rail < 0) rail = ends.length;
        ends[rail] = end; edge.rail = rail;
    }
    const railWidth = Math.min(56, width * .2, ends.length * 4);
    for (const node of nodes.values()) {
        const inset = 8 + (rows.indents[node.track] ? 18 : 0);
        const available = Math.max(1, width - 70 - railWidth - inset), count = rows.columns[node.track];
        node.gap = Math.min(12, available / count * .25);
        node.width = Math.min(96, available / count - node.gap);
        node.x = inset + node.column * (node.width + node.gap);
        node.y = node.track * row + (row + 18) / 2;
        // Nest long branches outside short ones. Visual port order is independent
        // of the original audio input slot, including repeated connections.
        node.outgoing.sort((a, b) => nodes.get(b.to).track - nodes.get(a.to).track ||
            nodes.get(b.to).column - nodes.get(a.to).column || a.slot - b.slot);
        node.incoming.sort((a, b) => nodes.get(a.from).track - nodes.get(b.from).track ||
            nodes.get(a.from).column - nodes.get(b.from).column || a.slot - b.slot);
    }
    const departures = [], arrivals = [];
    for (const node of nodes.values()) {
        (departures[node.track] ||= []).push(...node.outgoing.filter(e => !e.direct));
        (arrivals[node.track] ||= []).push(...node.incoming.filter(e => e.rail !== undefined).reverse());
    }
    const fraction = (list, edge) => (list.indexOf(edge) + 1) / (list.length + 1);
    const between = (a, b, t) => a + (b - a) * t;
    for (const edge of edges) {
        const a = nodes.get(edge.from), b = nodes.get(edge.to);
        const out = fraction(a.outgoing, edge), input = fraction(b.incoming, edge);
        const x = a.x + a.width, y = a.y + (out - .5) * 14, target = b.y + (input - .5) * 14;
        edge.points = [[x, y]];
        if (!edge.direct) {
            const leave = x + a.gap * (.15 + (1 - out) * .3);
            const enter = b.x - (b.column ? b.gap : 18) * (.15 + input * .3);
            const bottom = between(a.y + 11, (a.track + 1) * row - 1, fraction(departures[a.track], edge));
            edge.points.push([leave, y], [leave, bottom]);
            if (edge.rail !== undefined) {
                const rail = width - 66 - edge.rail * railWidth / Math.max(1, ends.length);
                const top = between(b.track * row + 18, b.y - 11, fraction(arrivals[b.track], edge));
                edge.points.push([rail, bottom], [rail, top], [enter, top]);
            } else edge.points.push([enter, bottom]);
            edge.points.push([enter, target]);
        }
        edge.points.push([b.x, target]);
    }
    return {nodes, edges};
}

// Start every source in the first rank, regardless of its downstream chain length.
function rankFromSources(graph) {
    const ranks = new Map();
    function rank(id) {
        if (!ranks.has(id)) ranks.set(id, Math.max(0, ...graph.inEdges(id).map(edge => rank(edge.v) + graph.edge(edge).minlen)));
        return graph.node(id).rank = ranks.get(id);
    }
    for (const id of graph.nodes()) rank(id);
}

// Fold track/master gain stages into their source, but keep explicit mixes.
// Layer by signal depth; shared nodes and repeated inputs retain their identity.
export function graphLayout(score) {
    const implicit = new Set(score.tracks.map(track => track[1]));
    function resolve(id) {
        while (implicit.has(id)) id = score.nodes[id].inputs[0];
        return id;
    }
    const nodes = new Map();
    score.nodes.forEach((node, id) => {
        if (!implicit.has(id)) nodes.set(id, {id, inputs: node.inputs.map(resolve), outputs: [], tracks: [],
            width: 192, height: Math.max(68, node.inputs.length * 14 + 20)});
    });
    for (const id of implicit) nodes.get(resolve(id)).tracks.push(id);
    // Sugiyama layout: ranking, crossing reduction, then coordinates and edge routes.
    const graph = new Graph({multigraph: true}).setGraph({rankdir: "LR", ranker: rankFromSources,
        nodesep: 28, edgesep: 14, ranksep: 96});
    for (const node of nodes.values()) graph.setNode(String(node.id), node);
    const edges = [];
    for (const node of nodes.values()) {
        node.incoming = [];
        node.inputs.forEach((from, slot) => {
            const edge = {from, to: node.id, slot};
            graph.setEdge(String(from), String(node.id), edge, String(slot));
            nodes.get(from).outputs.push(node.id);
            node.incoming.push(edge); edges.push(edge);
        });
    }
    if (nodes.size) layout(graph);
    for (const node of nodes.values()) {
        node.x -= node.width / 2; node.y -= node.height / 2;
        // Visual port order follows the routed cables; audio input indices stay intact.
        node.incoming.sort((a, b) => a.points.at(-2).y - b.points.at(-2).y || a.slot - b.slot);
    }
    for (const node of nodes.values()) node.incoming.forEach((edge, i) => {
        const source = nodes.get(edge.from);
        edge.points[0] = {x: source.x + source.width + 5, y: source.y + source.height / 2};
        edge.points[edge.points.length - 1] = {x: node.x - 5,
            y: node.y + node.height / 2 + (i - (node.inputs.length - 1) / 2) * 14};
    });
    return {nodes, edges, output: resolve(score.output), width: graph.graph().width || 0, height: graph.graph().height || 0};
}
