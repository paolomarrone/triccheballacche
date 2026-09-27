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
