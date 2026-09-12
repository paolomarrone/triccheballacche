// A short visual pulse also makes millisecond percussion notes visible between display frames.
export function activeLines(trace, path, seconds) {
    const lines = new Set();
    for (const [start, end, locations] of trace.events) {
        if (seconds < start || seconds >= Math.max(end, start + 0.08)) continue;
        for (const location of locations)
            for (const frame of trace.locations[location])
                if (frame.file.replace(/^\//, "") === path.replace(/^\//, "")) lines.add(frame.line);
    }
    return lines;
}
