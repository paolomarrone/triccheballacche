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
    const layers = [];
    function depth(node) {
        if (node.layer !== undefined) return node.layer;
        node.layer = node.inputs.length ? 1 + Math.max(...node.inputs.map(id => depth(nodes.get(id)))) : 0;
        (layers[node.layer] ||= []).push(node);
        return node.layer;
    }
    for (const node of nodes.values()) {
        depth(node);
        for (const id of node.inputs) nodes.get(id).outputs.push(node.id);
    }
    for (const [column, layer] of layers.entries()) {
        let y = 0;
        for (const node of layer) {
            node.x = column * 288; node.y = y;
            y += node.height + 28;
        }
    }
    function align(layer, direction) {
        const desired = node => {
            const neighbors = node[direction].map(id => nodes.get(id));
            return neighbors.length ? neighbors.reduce((sum, n) => sum + n.y + n.height / 2, 0) / neighbors.length : node.y + node.height / 2;
        };
        const centers = new Map(layer.map(node => [node.id, desired(node)]));
        layer.sort((a, b) => centers.get(a.id) - centers.get(b.id) || a.id - b.id);
        let bottom = -Infinity, shift = 0;
        for (const node of layer) {
            node.y = Math.max(bottom, centers.get(node.id) - node.height / 2);
            bottom = node.y + node.height + 28;
            shift += centers.get(node.id) - node.y - node.height / 2;
        }
        for (const node of layer) node.y += shift / layer.length;
    }
    for (let pass = 0; pass < 2; ++pass) {
        for (const layer of layers) align(layer, "inputs");
        for (const layer of [...layers].reverse()) align(layer, "outputs");
    }
    const placed = [...nodes.values()], top = nodes.size ? Math.min(...placed.map(node => node.y)) : 0;
    for (const node of placed) node.y -= top;
    return {nodes, output: resolve(score.output), width: Math.max(0, ...placed.map(n => n.x + n.width)),
        height: Math.max(0, ...placed.map(n => n.y + n.height))};
}
