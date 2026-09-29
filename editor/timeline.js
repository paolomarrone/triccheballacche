// A viewport in seconds, independent of score duration, with one bounded buffer around it.
import {automation} from "./automation.js";

export function timeline(request, select, selectTrack, seek, error) {
    const get = id => document.getElementById(id);
    const panel = get("timeline"), roll = get("roll");
    const headers = get("track-headers"), tracks = get("track-list"), follow = get("follow"), detail = get("note-info");
    const space = get("score-view"), canvas = get("score-canvas"), message = get("score-message");
    let score, data, from = 0, scale = 0.02, time = 0, playing = false;
    let width = 0, height = 0, row = 60, label = 220, version = 0, pending = false, scheduled = false;
    let loaded, latency = 0, position = 0, stamp = 0, animation = 0;
    let drag, selected, trackIndex = 0;
    let listening = [], outputs = [], enabled = false;
    const changing = new Set();
    const ruler = 24;
    const envelopes = automation(request, changed);
    let renderer, flat = true, disposed = false;

    function renderError(cause) {
        message.textContent = `Score view unavailable: ${cause.message || cause}`;
        message.hidden = false;
        get("score-3d").disabled = get("score-reset").disabled = true;
    }

    get("score-3d").onclick = () => {
        flat = !flat;
        get("score-3d").setAttribute("aria-pressed", !flat);
        get("score-reset").hidden = flat;
        get("automation-parameters").hidePopover();
        drag = undefined;
        changed();
    };
    get("score-reset").onclick = () => renderer?.home();
    space.addEventListener("wheel", event => {
        if (flat) {
            event.stopImmediatePropagation();
            wheel(event);
            return;
        }
        if (!event.shiftKey && !event.altKey) return;
        event.preventDefault(); event.stopImmediatePropagation();
        const unit = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? height : 1;
        follow.checked = false;
        if (event.altKey) zoom(Math.exp(event.deltaY * unit * .005));
        else move(from + (event.deltaX || event.deltaY) * unit * scale);
    }, {capture: true, passive: false});
    window.addEventListener("pagehide", () => { disposed = true; cancelAnimationFrame(animation); renderer?.dispose(); });

    function audible(index) {
        const node = score.nodes[score.tracks[index][1]];
        const solo = listening.reduce((mask, flags, i) => mask | (flags & 2 ? 1 << i : 0), 0);
        return !(listening[index] & 1) && (!solo || ((node.upstream | node.downstream) & solo));
    }

    function buttons() {
        for (const [index, track] of Array.from(tracks.children).entries()) {
            track.classList.toggle("inaudible", score.tracks[index][0] >= 0 && !audible(index));
            for (const button of track.querySelectorAll("[data-listen]")) {
                button.setAttribute("aria-pressed", Boolean(listening[index] & Number(button.dataset.listen)));
                button.disabled = !enabled || changing.has(index);
            }
            const control = track.querySelector(".track-automation");
            if (control) control.disabled = !enabled;
        }
    }

    async function listen(index, bit) {
        if (!enabled || changing.has(index)) return;
        const revision = score.revision, flags = listening[index] ^ bit;
        changing.add(index);
        buttons();
        try {
            await request("listen", revision, index, flags);
            if (revision === score?.revision) listening[index] = flags;
        } catch (cause) { error(cause); }
        finally {
            if (revision === score?.revision) {
                changing.delete(index);
                buttons();
                draw();
            }
        }
    }

    function viewport() {
        const span = Math.max(1, width - label) * scale;
        const all = !flat;
        return {from, to: from + span, span,
            first: all ? 0 : Math.floor(roll.scrollTop / row),
            count: all ? score?.tracks.length || 0 : Math.ceil((height - ruler + roll.scrollTop % row) / row),
            bins: Math.max(1, Math.min(512, Math.ceil((width - label) / 3)))};
    }

    function changed(invalidate = true) {
        if (invalidate) ++version;
        draw();
        if (!scheduled) {
            scheduled = true;
            requestAnimationFrame(() => { scheduled = false; load(); });
        }
    }

    function covers(buffer, view, margin = 0) {
        return buffer && buffer.span === view.span && buffer.first <= view.first &&
            buffer.first + buffer.count >= view.first + view.count &&
            buffer.from <= Math.max(0, view.from - margin) && buffer.to >= view.to + margin;
    }

    async function load() {
        if (!score || pending || disposed || width <= label || height <= ruler) return;
        const generation = version, revision = score.revision, view = viewport();
        const margin = Math.max(view.span / 2, latency * 2);
        if (loaded?.generation === generation && covers(loaded, view, margin)) return;
        // Refill before the visible window reaches the buffer edge. Moving within
        // the buffer does not invalidate in-flight notes or automation queries.
        const padding = Math.max(view.span, latency * 3), started = performance.now();
        view.from = Math.max(0, view.from - padding);
        view.to += padding;
        view.bins = Math.min(512, view.bins * 3);
        pending = true;
        try {
            const result = {revision, from: view.from, to: view.to, first: view.first, lanes: []};
            const end = Math.min(score.tracks.length, view.first + view.count);
            for (let first = view.first; first < end; first += 8) {
                const part = await request("range", revision, String(view.from), String(view.to), first, Math.min(8, view.first + view.count - first), view.bins);
                if (part.stale || part.revision !== revision || score.revision !== revision) return;
                result.lanes.push(...part.lanes);
                // Keep the previous complete window when a newer viewport supersedes a batch.
                if ((generation !== version || !covers(view, viewport())) && first + 8 < end) return;
            }
            if (revision === score?.revision) {
                const current = () => generation === version && covers(view, viewport());
                if (!current()) return;
                const curves = await envelopes.query(view, revision, current);
                if (revision === score.revision && current()) {
                    // Publish a complete buffer, including all requested lanes and curves.
                    data = result;
                    loaded = {...view, generation};
                    panel.dataset.revision = revision;
                    envelopes.accept(curves);
                    // Account for the bridge only after a complete, current refill.
                    latency = Math.max((performance.now() - started) / 1000, latency * .75);
                    draw();
                }
            }
        } catch (cause) { error(cause); }
        finally {
            pending = false;
            if (generation !== version || !covers(view, viewport())) load();
        }
    }

    function name(track) {
        return score.nodes[track[1]].label || (track[0] < 0 ? "Master" : score.nodes[track[0]].name);
    }

    function destinations(id, seen = new Set(), names = new Set()) {
        if (seen.has(id)) return names;
        seen.add(id);
        if (id === score.output) names.add("out");
        for (const next of outputs[id]) {
            const track = score.tracks.find(t => t[1] === next);
            if (track) names.add(name(track));
            else destinations(next, seen, names);
        }
        return names;
    }

    function chain(track) {
        const effects = track.slice(2).map(id => score.nodes[id].name);
        return [name(track), ...effects, [...destinations(track[1])].join(" + ")].join(" → ");
    }

    function pitchRange(track) {
        const node = score.nodes[track[0]];
        const low = node?.low ?? 48, high = node?.high ?? 72;
        const padding = Math.max(2, (12 - (high - low)) / 2);
        return [Math.max(0, low - padding), Math.min(127, high + padding)];
    }

    function draw() {
        tracks.style.transform = `translateY(${-roll.scrollTop}px)`;
        const view = viewport(), dark = matchMedia("(prefers-color-scheme: dark)").matches;
        renderer?.update({score, data, view, curves: envelopes.curves(view),
            time, playing, selected, audible, dark, flat, pitchRange,
            layout: {row, ruler, scroll: roll.scrollTop, width: width - label}});
    }

    function resize() {
        width = roll.clientWidth;
        height = roll.clientHeight;
        label = Math.min(230, Math.round(width * 0.3));
        panel.style.setProperty("--track-label", `${label}px`);
        space.style.right = `${roll.offsetWidth - width}px`;
        headers.style.width = `${label}px`;
        headers.style.height = `${Math.max(0, height - ruler)}px`;
        headers.style.marginBottom = `${-Math.max(0, height - ruler)}px`;
        tracks.style.setProperty("--track-height", `${row}px`);
        tracks.classList.toggle("compact", row < 54);
        get("lanes-space").style.height = `${Math.max(height, ruler + (score?.tracks.length || 0) * row)}px`;
        changed();
    }

    function move(value, nextScale = scale) {
        value = Math.max(0, value);
        if (!Number.isFinite(value) || value + nextScale === value) return;
        from = value;
        scale = nextScale;
        changed(false);
    }

    function zoom(factor, x = (width - label) / 2) {
        const next = Math.max(0.002, Math.min(3600, scale * factor));
        const anchor = from + x * scale;
        move(anchor - x * next, next);
    }

    function animate(now) {
        animation = 0;
        // Interpolate between transport reports, but stop extrapolating if the
        // backend stalls. Audio remains the authority for musical time.
        time = playing ? Math.max(time, position + Math.min(.1, Math.max(0, now - stamp) / 1000)) : position;
        if (score && playing && follow.checked) move(time - viewport().span * .15);
        else draw();
        if (playing && !disposed) animation = requestAnimationFrame(animate);
    }

    follow.onchange = () => { if (follow.checked) move(time - viewport().span * .15); };

    roll.addEventListener("scroll", () => changed(false));
    function wheel(event) {
        event.preventDefault();
        const [x, y] = point(event), unit = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? height : 1;
        const dy = event.deltaY * unit, dx = event.deltaX * unit;
        if (event.ctrlKey || event.metaKey) {
            const anchor = (roll.scrollTop + Math.max(0, y - ruler)) / row;
            row = Math.max(44, Math.min(480, row * Math.exp(-dy * 0.005)));
            resize();
            roll.scrollTop = Math.max(0, anchor * row - Math.max(0, y - ruler));
            changed();
        } else if (headers.contains(event.target)) {
            roll.scrollTop += dy;
        } else {
            follow.checked = false;
            if (event.shiftKey || Math.abs(dx) > Math.abs(dy)) move(from + (dx || dy) * scale);
            else zoom(Math.exp(dy * 0.005), Math.max(0, x - label));
        }
    }
    roll.addEventListener("wheel", wheel, {passive: false});
    const point = event => { const rect = roll.getBoundingClientRect(); return [event.clientX - rect.left, event.clientY - rect.top]; };
    const curveAt = (x, y) => x >= label && x <= width && y >= ruler && y < height ?
        envelopes.hit(viewport(), {row, ruler, scroll: roll.scrollTop}, y, from + (x - label) * scale) : null;
    canvas.onpointerdown = event => {
        if (event.button || point(event)[0] < label) return;
        drag = {x: event.clientX, y: event.clientY, from, scroll: roll.scrollTop, moved: false};
        if (flat) event.currentTarget.setPointerCapture(event.pointerId);
    };
    canvas.onpointermove = event => {
        const [x, y] = point(event);
        if (drag && (drag.moved || Math.hypot(event.clientX - drag.x, event.clientY - drag.y) > 3)) {
            drag.moved = true;
            if (flat) {
                follow.checked = false;
                roll.scrollTop = drag.scroll + drag.y - event.clientY;
                move(drag.from + (drag.x - event.clientX) * scale);
            }
        }
        const found = renderer?.hit(event), lane = Math.floor((y - ruler + roll.scrollTop) / row);
        const curve = flat && curveAt(x, y);
        event.currentTarget.title = flat && y < ruler && x >= label ? "Click to seek" : curve ? curve.text :
            found?.note ? `Track ${found.track + 1} · ` + noteText(found.note) :
            found?.count ? `${found.count} notes · zoom in for individual notes` :
            !flat ? found ? `Track ${found.track + 1} · click to open plugins` : renderer?.help || "" :
            score?.tracks[lane] ? chain(score.tracks[lane]) : "";
    };
    async function choose(found, curve) {
        if (curve) {
            detail.textContent = curve.text;
            detail.title = "";
            if (curve.order < 0) return;
        }
        if (!curve && !found) return;
        const revision = score.revision;
        selected = curve ? {node: curve.node, order: curve.order} : {node: found.node, order: found.note[0], start: found.note[1]};
        const selection = selected;
        if (!curve) detail.textContent = noteText(found.note);
        draw();
        try {
            const result = await request("origin", revision, selection.node, selection.order);
            if (revision !== score.revision || selected !== selection || result.stale) return;
            detail.title = result.frames.map(([file, line, column]) => `${file}:${line}:${column}`).join("\n");
            if (result.truncated) detail.title += "\nPartial origins";
            select(result.frames);
        } catch (cause) { error(cause); }
    }

    function chooseTrack(index) {
        trackIndex = index;
        for (const button of tracks.querySelectorAll(".track-select")) button.setAttribute("aria-pressed", Number(button.dataset.track) === index);
        selectTrack(index);
    }

    canvas.onpointerup = event => {
        if (!drag) return;
        const moved = drag.moved;
        drag = undefined;
        if (moved) return;
        const [x, y] = point(event);
        if (flat && enabled && y >= 0 && y < ruler && x >= label) {
            seek(Math.min(score.end || Infinity, from + (x - label) * scale));
            return;
        }
        const curve = flat && curveAt(x, y), found = renderer?.hit(event);
        if (curve) choose(null, curve);
        else if (found && found.revision === score?.revision) {
            if (found.note) choose({node: score.tracks[found.track][0], note: found.note});
            else if (!found.count) chooseTrack(found.track);
            else { follow.checked = false; zoom(.5, ((found.a + found.b) / 2 - from) / scale); }
        }
    };
    canvas.onpointercancel = () => { drag = undefined; };
    canvas.ondblclick = event => {
        if (flat && point(event)[0] >= label && point(event)[1] >= ruler) { follow.checked = false; zoom(0.5, point(event)[0] - label); }
    };
    canvas.onkeydown = event => {
        if (["ArrowLeft", "ArrowRight", "Home", "+", "-"].includes(event.key)) {
            event.preventDefault();
            if (event.key === "+" || event.key === "-") zoom(event.key === "+" ? 0.5 : 2);
            else if (event.key === "Home") { move(0); if (enabled) seek(0); }
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

    import("./score-space.js").then(({scoreSpace}) => {
        if (disposed) return;
        renderer = scoreSpace(canvas, message, renderError);
        get("score-3d").disabled = get("score-reset").disabled = false;
        draw();
    }).catch(renderError);

    return {
        status(prepared, busy) {
            enabled = prepared && !busy;
            buttons();
        },
        score(value) {
            score = value; data = selected = undefined; from = 0; latency = 0; roll.scrollTop = 0;
            envelopes.score(score);
            listening = score.tracks.map(() => 0);
            outputs = score.nodes.map(() => []);
            score.nodes.forEach((node, id) => node.inputs.forEach(input => outputs[input].push(id)));
            changing.clear();
            detail.textContent = ""; detail.title = "";
            trackIndex = Math.min(trackIndex, Math.max(0, score.tracks.length - 1));
            tracks.replaceChildren(...score.tracks.map((track, index) => {
                const lane = document.createElement("div");
                lane.className = "track";
                const button = document.createElement("button"), title = document.createElement("span"), effects = document.createElement("small");
                button.className = "track-select";
                title.textContent = `${index + 1}  ${name(track)}`;
                effects.textContent = chain(track).slice(name(track).length + 1);
                button.title = chain(track);
                button.dataset.track = index;
                button.setAttribute("aria-pressed", index === trackIndex);
                button.append(title, effects);
                button.onclick = () => chooseTrack(index);
                const heading = document.createElement("div");
                heading.className = "track-heading";
                heading.append(button, envelopes.control(index));
                lane.append(heading);
                if (track[0] >= 0) {
                    const controls = document.createElement("div");
                    controls.className = "track-listen";
                    for (const [label, bit, title] of [["M", 1, "Mute"], ["S", 2, "Solo"]]) {
                        const toggle = document.createElement("button");
                        toggle.textContent = label;
                        toggle.dataset.listen = bit;
                        toggle.title = `${title} track ${index + 1}`;
                        toggle.setAttribute("aria-label", toggle.title);
                        toggle.onclick = () => listen(index, bit);
                        controls.append(toggle);
                    }
                    lane.append(controls);
                }
                return lane;
            }));
            buttons();
            resize();
        },
        revise(value) {
            score = value;
            envelopes.score(score, true);
            for (const [index, lane] of Array.from(tracks.children).entries())
                lane.querySelector(".track-automation").replaceWith(envelopes.control(index));
            selected = undefined;
            buttons();
            changed();
        },
        position(seconds, active, locate = false) {
            if (position === seconds && playing === active && !locate) return;
            if (locate || !playing || seconds < position) time = seconds;
            position = seconds; playing = active; stamp = performance.now();
            const view = viewport();
            if (score && locate && (seconds < from || seconds > from + view.span * .85)) from = Math.max(0, seconds - view.span * .15);
            if (locate) changed(false);
            cancelAnimationFrame(animation);
            animate(stamp);
        }
    };
}

function noteText([, start, end, pitch, velocity]) {
    const name = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"][pitch % 12];
    return `${name}${Math.floor(pitch / 12) - 1} · MIDI ${pitch} · v${velocity} · ${start.toFixed(3)}–${end.toFixed(3)} s`;
}
