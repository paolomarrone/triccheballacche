// A viewport in seconds, independent of score duration. Only visible lanes and intervals cross the bridge.
export function timeline(request, select, error) {
    const get = id => document.getElementById(id);
    const panel = get("timeline"), roll = get("roll"), canvas = get("notes"), context = canvas.getContext("2d");
    const start = get("view-start"), follow = get("follow"), detail = get("note-info");
    let score, data, from = 0, scale = 0.02, time = 0, playing = false;
    let width = 0, height = 0, row = 60, label = 220, version = 0, pending = false, scheduled = false;
    let hits = [], drag, selected;
    const ruler = 24;

    function viewport() {
        return {from, to: from + Math.max(1, width - label) * scale,
            first: Math.floor(roll.scrollTop / row), count: Math.min(8, Math.ceil((height - ruler + roll.scrollTop % row) / row)),
            bins: Math.max(1, Math.min(512, Math.ceil((width - label) / 3)))};
    }

    function changed() {
        ++version;
        data = undefined;
        start.value = String(Number(from.toFixed(3)));
        draw();
        if (!scheduled) {
            scheduled = true;
            requestAnimationFrame(() => { scheduled = false; load(); });
        }
    }

    async function load() {
        if (!score || pending || width <= label || height <= ruler) return;
        const generation = version, revision = score.revision, view = viewport();
        pending = true;
        try {
            const result = await request("range", revision, String(view.from), String(view.to), view.first, view.count, view.bins);
            if (generation === version && revision === score?.revision && !result.stale && result.revision === revision) {
                data = result;
                panel.dataset.revision = revision;
                draw();
            }
        } catch (cause) { error(cause); }
        finally {
            pending = false;
            if (generation !== version) load();
        }
    }

    function chain(track) {
        const [source, , ...effects] = track;
        return [source < 0 ? "Master" : score.nodes[source].name, ...effects.map(id => score.nodes[id].name)].join(" → ");
    }

    function pitchRange(track) {
        const node = score.nodes[track[0]];
        const low = node?.low ?? 48, high = node?.high ?? 72;
        const padding = Math.max(2, (12 - (high - low)) / 2);
        return [Math.max(0, low - padding), Math.min(127, high + padding)];
    }

    function draw() {
        const css = getComputedStyle(panel), foreground = css.color;
        const dark = matchMedia("(prefers-color-scheme: dark)").matches;
        const grid = dark ? "#383b40" : "#e0e3e6", ink = dark ? "#adb3ba" : "#59616a";
        context.clearRect(0, 0, width, height);
        context.font = "12px system-ui";
        context.textBaseline = "middle";
        hits = [];
        if (!score) {
            context.fillStyle = ink;
            context.fillText("Esegui una partitura per vedere le note", 12, Math.min(height / 2, 45));
            return;
        }
        const view = viewport(), rawStep = scale * 85, power = 10 ** Math.floor(Math.log10(rawStep));
        const step = [1, 2, 5, 10].find(n => n * power >= rawStep) * power;
        context.save();
        context.beginPath(); context.rect(label, 0, width - label, height); context.clip();
        const firstTick = Math.ceil(from / step);
        for (let i = 0; i < Math.ceil((width - label) / 85) + 1; ++i) {
            const tick = (firstTick + i) * step;
            const x = label + (tick - from) / scale;
            context.fillStyle = grid; context.fillRect(Math.round(x), ruler, 1, height - ruler);
            context.fillStyle = ink;
            context.fillText(`${Number(tick.toFixed(Math.max(0, -Math.floor(Math.log10(step)))))} s`, x + 4, ruler / 2);
        }
        context.restore();
        for (let i = view.first; i < Math.min(score.tracks.length, view.first + view.count); ++i) {
            const y = ruler + i * row - roll.scrollTop, track = score.tracks[i];
            const lane = data?.lanes[i - data.first], [low, high] = pitchRange(track);
            const pitchY = pitch => y + row - 8 - (pitch - low) * (row - 16) / (high - low + 1);
            const noteHeight = Math.max(2, Math.min(9, (row - 16) / (high - low + 1)));
            context.save();
            context.beginPath(); context.rect(0, Math.max(ruler, y), width, Math.min(row, height - y)); context.clip();
            context.fillStyle = grid; context.fillRect(0, y + row - 1, width, 1);
            context.save();
            context.beginPath(); context.rect(8, y, label - 18, row); context.clip();
            context.fillStyle = foreground;
            context.fillText(`${i + 1}  ${track[0] < 0 ? "Master" : score.nodes[track[0]].name}`, 8, y + 19);
            context.fillStyle = ink;
            context.fillText(track.length > 2 ? track.slice(2).map(id => score.nodes[id].name).join(" → ") : "→ out", 8, y + 37);
            context.restore();
            context.beginPath(); context.rect(label, ruler, width - label, height - ruler); context.clip();
            const color = `hsl(${(i * 57 + 190) % 360} 58% ${dark ? 63 : 43}%)`;
            if (lane?.notes) for (const note of lane.notes) {
                const [order, a, b, pitch, velocity] = note;
                const x = Math.max(label, label + (a - from) / scale), right = Math.min(width, label + (b - from) / scale);
                const top = pitchY(pitch) - noteHeight;
                context.fillStyle = color;
                context.globalAlpha = 0.4 + 0.6 * velocity / 127;
                context.fillRect(x, top, Math.max(1, right - x), noteHeight);
                context.globalAlpha = 1;
                if (selected?.node === track[0] && selected?.order === order) {
                    context.strokeStyle = foreground;
                    context.strokeRect(x - 1, top - 1, Math.max(1, right - x) + 2, noteHeight + 2);
                }
                hits.push({x, y: top - 2, w: Math.max(3, right - x), h: noteHeight + 4, node: track[0], note});
            }
            if (lane?.density) {
                const binWidth = (width - label) / lane.density.length;
                lane.density.forEach(([count, min, max], bin) => {
                    if (!count) return;
                    context.fillStyle = color;
                    context.globalAlpha = Math.min(0.9, 0.2 + Math.log2(count + 1) / 12);
                    context.fillRect(label + bin * binWidth, pitchY(max) - noteHeight, Math.max(1, binWidth),
                        Math.max(noteHeight, pitchY(min) - pitchY(max) + noteHeight));
                });
                context.globalAlpha = 1;
                context.fillStyle = foreground;
                context.fillText(`${lane.count} note · densità · doppio clic per avvicinare`, label + 8, y + 12);
            }
            context.restore();
        }
        context.fillStyle = grid; context.fillRect(label - 1, 0, 1, height); context.fillRect(0, ruler - 1, width, 1);
        if (Number.isFinite(score.end) && score.end >= from && score.end < view.to) {
            context.strokeStyle = ink; context.setLineDash([3, 4]);
            const x = label + (score.end - from) / scale;
            context.beginPath(); context.moveTo(x, ruler); context.lineTo(x, height); context.stroke(); context.setLineDash([]);
        }
        if (time >= from && time < view.to) {
            context.fillStyle = dark ? "#ffd377" : "#995b00";
            context.fillRect(Math.round(label + (time - from) / scale), 0, 2, height);
        }
        canvas.dataset.notes = hits.length;
        canvas.dataset.dense = Boolean(data?.lanes.some(lane => lane.density));
        canvas.setAttribute("aria-label", `Note da ${from.toFixed(2)} a ${view.to.toFixed(2)} secondi. Tracce ${score.tracks.length ? view.first + 1 : 0}–${Math.min(score.tracks.length, view.first + view.count)}.`);
    }

    function resize() {
        width = roll.clientWidth;
        height = roll.clientHeight;
        label = Math.min(230, Math.round(width * 0.3));
        row = Math.max(58, Math.ceil((height - ruler) / 7));
        const ratio = devicePixelRatio;
        canvas.width = Math.round(width * ratio); canvas.height = Math.round(height * ratio);
        canvas.style.width = `${width}px`; canvas.style.height = `${height}px`; canvas.style.marginBottom = `${-height}px`;
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        get("lanes-space").style.height = `${Math.max(height, ruler + (score?.tracks.length || 0) * row)}px`;
        changed();
    }

    function move(value, nextScale = scale) {
        value = Math.max(0, value);
        if (!Number.isFinite(value) || value + nextScale === value) {
            start.value = String(from);
            return;
        }
        from = value;
        scale = nextScale;
        changed();
    }

    function zoom(factor, x = (width - label) / 2) {
        const next = Math.max(0.002, Math.min(3600, scale * factor));
        const anchor = from + x * scale;
        move(anchor - x * next, next);
    }

    start.addEventListener("change", () => { follow.checked = false; move(start.valueAsNumber); });
    get("zoom-in").onclick = () => zoom(0.5);
    get("zoom-out").onclick = () => zoom(2);
    for (const [id, direction] of [["pan-left", -1], ["pan-right", 1]]) get(id).onclick = () => {
        follow.checked = false; move(from + direction * (viewport().to - from) * 0.8);
    };
    roll.addEventListener("scroll", changed);
    roll.addEventListener("wheel", event => {
        if (event.ctrlKey || event.metaKey) {
            event.preventDefault(); follow.checked = false;
            zoom(Math.exp(event.deltaY * 0.005), Math.max(0, event.offsetX - label));
        } else if (event.shiftKey || Math.abs(event.deltaX) > Math.abs(event.deltaY)) {
            event.preventDefault(); follow.checked = false; move(from + (event.deltaX || event.deltaY) * scale);
        }
    }, {passive: false});
    const point = event => { const rect = canvas.getBoundingClientRect(); return [event.clientX - rect.left, event.clientY - rect.top]; };
    const hit = (x, y) => hits.findLast(h => x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h);
    canvas.onpointerdown = event => {
        if (event.button || point(event)[0] < label) return;
        drag = {x: event.clientX, from, moved: false}; canvas.setPointerCapture(event.pointerId);
    };
    canvas.onpointermove = event => {
        const [x, y] = point(event);
        if (drag && (drag.moved || Math.abs(event.clientX - drag.x) > 3)) {
            drag.moved = true; follow.checked = false; move(drag.from + (drag.x - event.clientX) * scale);
        }
        const found = hit(x, y), lane = Math.floor((y - ruler + roll.scrollTop) / row);
        canvas.title = found ? noteText(found.note) : score?.tracks[lane] ? chain(score.tracks[lane]) : "";
    };
    canvas.onpointerup = async event => {
        const moved = drag?.moved;
        drag = undefined;
        if (moved) return;
        const found = hit(...point(event));
        if (!found) return;
        const revision = score.revision;
        selected = {node: found.node, order: found.note[0]};
        const selection = selected;
        detail.textContent = noteText(found.note);
        draw();
        try {
            const result = await request("note", revision, found.node, found.note[0]);
            if (revision !== score.revision || selected !== selection || result.stale) return;
            detail.title = result.frames.map(([file, line, column]) => `${file}:${line}:${column}`).join("\n");
            if (result.truncated) detail.title += "\nOrigini parziali";
            select(result.frames);
        } catch (cause) { error(cause); }
    };
    canvas.onpointercancel = () => { drag = undefined; };
    canvas.ondblclick = event => { if (point(event)[0] >= label) { follow.checked = false; zoom(0.5, point(event)[0] - label); } };
    canvas.onkeydown = event => {
        if (["ArrowLeft", "ArrowRight", "+", "-"].includes(event.key)) {
            event.preventDefault();
            if (event.key === "+" || event.key === "-") zoom(event.key === "+" ? 0.5 : 2);
            else { follow.checked = false; move(from + (event.key === "ArrowLeft" ? -1 : 1) * (viewport().to - from) / 2); }
        }
    };
    const split = get("split"), main = panel.parentElement;
    const size = value => { panel.style.flexBasis = `${Math.max(110, Math.min(main.clientHeight - 100, value))}px`; };
    split.onpointerdown = event => { split.setPointerCapture(event.pointerId); };
    split.onpointermove = event => { if (split.hasPointerCapture(event.pointerId)) size(panel.getBoundingClientRect().bottom - event.clientY); };
    split.onkeydown = event => {
        if (event.key === "ArrowUp" || event.key === "ArrowDown") {
            event.preventDefault(); size(panel.clientHeight + (event.key === "ArrowUp" ? 20 : -20));
        }
    };
    new ResizeObserver(resize).observe(roll);
    matchMedia("(prefers-color-scheme: dark)").addEventListener("change", draw);

    return {
        score(value) {
            score = value; data = selected = undefined; from = 0; roll.scrollTop = 0;
            detail.textContent = ""; detail.title = "";
            resize();
        },
        position(seconds, active) {
            if (time === seconds && playing === active) return;
            time = seconds; playing = active;
            const span = viewport().to - from;
            if (score && active && follow.checked && (time < from || time > from + span * 0.85)) move(time - span * 0.15);
            else draw();
        }
    };
}

function noteText([, start, end, pitch, velocity]) {
    const name = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"][pitch % 12];
    return `${name}${Math.floor(pitch / 12) - 1} · MIDI ${pitch} · v${velocity} · ${start.toFixed(3)}–${end.toFixed(3)} s`;
}
