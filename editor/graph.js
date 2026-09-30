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
    const size = 84, gap = 8, room = width - 70 - railWidth;
    // Keep each chain tight. Give cross-lane dependencies the extra room instead
    // of stretching every connection by the same amount.
    function expand(separation) {
        const positions = new Map(), levels = new Map(), last = [];
        for (const node of nodes.values()) {
            let x = last[node.track]?.x ?? 8 + (rows.indents[node.track] ? 18 : 0);
            let depth = last[node.track]?.depth || 0;
            for (const edge of node.incoming) {
                const cross = nodes.get(edge.from).track !== node.track;
                x = Math.max(x, positions.get(edge.from) + size + (cross ? separation : gap));
                depth = Math.max(depth, levels.get(edge.from) + Number(cross));
            }
            positions.set(node.id, x); levels.set(node.id, depth);
            last[node.track] = {x: x + size + gap, depth};
        }
        return {positions, end: Math.max(0, ...positions.values()) + size, depth: Math.max(1, ...levels.values())};
    }
    const compact = 8 + Math.max(1, ...rows.columns.map((count, i) => count * (size + gap) - gap + (rows.indents[i] ? 18 : 0)).filter(Number.isFinite));
    const separation = Math.max(24, railWidth + 12), base = expand(separation);
    const expanded = expand(separation + Math.min(40, Math.max(0, room - base.end) / base.depth));
    const spread = Math.max(0, Math.min(1, (room - compact) / Math.max(1, base.end - compact)));
    const lanes = [];
    for (const node of nodes.values()) {
        const inset = 8 + (rows.indents[node.track] ? 18 : 0);
        const available = Math.max(1, width - 70 - railWidth - inset), count = rows.columns[node.track];
        node.gap = Math.min(gap, available / count * .25);
        node.width = Math.min(size, available / count - node.gap);
        node.x = inset + node.column * (node.width + node.gap);
        node.y = node.track * row + (row + 18) / 2;
        node.x += (expanded.positions.get(node.id) - node.x) * spread;
        (lanes[node.track] ||= []).push(node);
    }
    // Prefer passages between columns. Move a node's incoming cables together:
    // mixing an inside path with an outside return would cross its other inputs.
    const passages = [], groups = new Map();
    const distance = e => Math.abs(nodes.get(e.from).track - nodes.get(e.to).track);
    for (const edge of cross) {
        if (!groups.has(edge.to)) groups.set(edge.to, []);
        groups.get(edge.to).push(edge);
    }
    const nearest = group => Math.min(...group.map(distance));
    for (const group of [...groups.values()].sort((a, b) => nearest(a) - nearest(b))) {
        const saved = passages.length;
        for (const edge of group.sort((a, b) => distance(b) - distance(a) || a.slot - b.slot)) {
            const a = nodes.get(edge.from), b = nodes.get(edge.to), [start, end] = bounds(edge);
            const next = lanes[a.track][a.column + 1], previous = lanes[b.track][b.column - 1];
            const low = Math.max(a.x + a.width + 6, previous ? previous.x + previous.width + 6 : 0);
            let turn = Math.min(b.x - 6, next ? next.x - 6 : Infinity);
            const blocked = [...nodes.values()].filter(n => n.track >= start && n.track <= end).map(n => [n.x - 4, n.x + n.width + 4]);
            for (const p of passages) if (p.start <= end && p.end >= start) blocked.push([p.x - 4, p.x + 4]);
            blocked.sort((a, b) => b[1] - a[1]);
            for (const [left, right] of blocked) if (turn > left && turn < right) turn = left;
            if (turn < low) {
                for (const edge of group) delete edge.forward;
                passages.length = saved;
                break;
            }
            edge.forward = turn; passages.push({start, end, x: turn});
        }
    }
    for (const node of nodes.values()) {
        // Nest long branches outside short ones. Visual port order is independent
        // of the original audio input slot, including repeated connections.
        const order = e => nodes.get(e.to).track * (e.forward === undefined ? -1 : 1);
        node.outgoing.sort((a, b) => order(a) - order(b) ||
            nodes.get(b.to).column - nodes.get(a.to).column || a.slot - b.slot);
        node.incoming.sort((a, b) => nodes.get(a.from).track - nodes.get(b.from).track ||
            nodes.get(a.from).column - nodes.get(b.from).column || a.slot - b.slot);
    }
    const departures = [], arrivals = [];
    for (const node of nodes.values()) {
        (departures[node.track] ||= []).push(...node.outgoing.filter(e => !e.direct && e.forward === undefined));
        (arrivals[node.track] ||= []).push(...node.incoming.filter(e => e.rail !== undefined && e.forward === undefined).reverse());
    }
    const fraction = (list, edge) => (list.indexOf(edge) + 1) / (list.length + 1);
    const between = (a, b, t) => a + (b - a) * t;
    for (const edge of edges) {
        const a = nodes.get(edge.from), b = nodes.get(edge.to);
        const out = fraction(a.outgoing, edge), input = fraction(b.incoming, edge);
        const x = a.x + a.width, y = a.y + (out - .5) * 14, target = b.y + (input - .5) * 14;
        edge.points = [[x, y]];
        if (edge.forward !== undefined) edge.points.push([edge.forward, y], [edge.forward, target]);
        else if (!edge.direct) {
            const enter = b.x - (b.column ? b.gap : 18) * (.15 + input * .3);
            let exit = y;
            // Only bypass blocks when there are more of them to the right.
            if (a.column + 1 < rows.columns[a.track]) {
                const leave = x + a.gap * (.15 + (1 - out) * .3);
                exit = between(a.y + 11, (a.track + 1) * row - 1, fraction(departures[a.track], edge));
                edge.points.push([leave, y], [leave, exit]);
            }
            if (edge.rail !== undefined) {
                const top = between(b.track * row + 18, b.y - 11, fraction(arrivals[b.track], edge));
                edge.bend = edge.points.length;
                edge.points.push([0, exit], [0, top], [enter, top]);
            } else edge.points.push([enter, exit]);
            edge.points.push([enter, target]);
        }
        edge.points.push([b.x, target]);
    }
    // Pack outside returns against the obstacles they actually pass. Inner
    // returns go first, so overlapping spans keep their nesting and separation.
    const returns = [], spacing = railWidth / Math.max(1, ends.length);
    for (const edge of cross.filter(e => e.forward === undefined).sort((a, b) => b.rail - a.rail)) {
        const a = edge.points[edge.bend], b = edge.points[edge.bend + 1];
        const low = Math.min(a[1], b[1]), high = Math.max(a[1], b[1]);
        let x = Math.max(edge.points[edge.bend - 1][0], edge.points[edge.bend + 2][0]) + 4;
        for (const node of nodes.values()) if (node.y + 11 > low && node.y - 11 < high)
            x = Math.max(x, node.x + node.width + 4);
        for (const other of returns) if (other.low <= high && other.high >= low) x = Math.max(x, other.x + spacing);
        for (const other of cross) if (other.forward !== undefined) {
            const y1 = other.points[1][1], y2 = other.points[2][1];
            if (Math.min(y1, y2) <= high && Math.max(y1, y2) >= low)
                x = Math.max(x, other.forward + spacing);
        }
        a[0] = b[0] = x;
        returns.push({low, high, x});
    }
    return {nodes, edges};
}

// Fold track/master gain stages into their source, but keep explicit mixes.
// Shared nodes and repeated inputs retain their identity.
export function signalGraph(score) {
    const implicit = new Set(score.tracks.map(track => track[1]));
    function resolve(id) {
        while (implicit.has(id)) id = score.nodes[id].inputs[0];
        return id;
    }
    const nodes = new Map();
    score.nodes.forEach((node, id) => {
        if (!implicit.has(id)) nodes.set(id, {id, inputs: node.inputs.map(resolve), outputs: []});
    });
    const edges = [];
    for (const node of nodes.values())
        node.inputs.forEach((from, slot) => {
            nodes.get(from).outputs.push(node.id);
            edges.push({from, to: node.id, slot});
        });
    return {nodes, edges};
}
