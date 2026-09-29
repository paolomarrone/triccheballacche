import * as THREE from "three";
import {OrbitControls} from "./vendor/three/OrbitControls.js";
import {automationRatio} from "./automation.js";

// A renderer of the shared viewport. It owns no score queries, transport or audition state.
export function scoreSpace(canvas, message, fail) {
    const length = 80, spacing = 5;
    const renderer = new THREE.WebGLRenderer({canvas, antialias: true});
    const scene = new THREE.Scene(), camera = new THREE.PerspectiveCamera(38, 1, 0.1, 2000);
    const plan = new THREE.OrthographicCamera(-length / 2, length / 2, 1, -1, .1, 2000);
    plan.position.z = 100;
    scene.add(new THREE.HemisphereLight("#ffffff", "#455166", 2));
    const light = new THREE.DirectionalLight("#ffffff", 2);
    light.position.set(-30, 80, 40); scene.add(light);
    const controls = new OrbitControls(camera, canvas);
    controls.maxPolarAngle = Math.PI * 0.49;
    controls.minDistance = 8;
    controls.maxDistance = 800;
    const ray = new THREE.Raycaster(), pointer = new THREE.Vector2(), transform = new THREE.Object3D(), tint = new THREE.Color();
    let world = new THREE.Group(), content = new THREE.Group(), head, rulerBand;
    let meshes = [], targets = [], ticks = [], state, previous, visible = false, flat = false;
    let frame = 0, lost = false, reset = true, origin = 0, trackCount;
    const sides = [new THREE.Plane(new THREE.Vector3(1, 0, 0), length / 2),
        new THREE.Plane(new THREE.Vector3(-1, 0, 0), length / 2)];
    const ceiling = new THREE.Plane(new THREE.Vector3(0, -1, 0), 0);
    let clip = sides;
    renderer.localClippingEnabled = true;
    scene.add(world); world.add(content);
    const help = "Drag to orbit · right-drag to pan · wheel to zoom · Shift+wheel to pan time · Alt+wheel to zoom time";
    canvas.title = help;

    function color(index) {
        return `hsl(${(index * 57 + 190) % 360}, 58%, ${state.dark ? 63 : 43}%)`;
    }

    function clear() {
        world.traverse(object => {
            if (object.isInstancedMesh) object.dispose();
            object.geometry?.dispose();
            object.material?.map?.dispose();
            object.material?.dispose();
        });
        scene.remove(world);
        world = new THREE.Group(); content = new THREE.Group(); scene.add(world); world.add(content);
        meshes = []; targets = []; ticks = []; head = rulerBand = undefined;
    }

    function line(points, color, opacity = 1, parent = content) {
        const mesh = new THREE.Line(new THREE.BufferGeometry().setFromPoints(points.map(p => new THREE.Vector3(...p))),
            new THREE.LineBasicMaterial({color, transparent: true, opacity, clippingPlanes: parent === content ? clip : null}));
        parent.add(mesh);
        return mesh;
    }

    function label(text, x, y, z, color, parent = world) {
        const image = document.createElement("canvas");
        const ctx = image.getContext("2d");
        ctx.font = "32px system-ui";
        image.width = Math.ceil(ctx.measureText(text).width) + 12; image.height = 64;
        ctx.font = "32px system-ui"; ctx.textAlign = "center"; ctx.fillStyle = color;
        ctx.fillText(text, image.width / 2, 44);
        const texture = new THREE.CanvasTexture(image);
        texture.colorSpace = THREE.SRGBColorSpace;
        const sprite = new THREE.Sprite(new THREE.SpriteMaterial({map: texture, depthTest: false}));
        const size = flat ? 24 * length / state.layout.width : 3;
        sprite.position.set(x, y, z); sprite.scale.set(image.width / image.height * size, size, 1);
        if (flat) sprite.renderOrder = 4;
        parent.add(sprite);
        return sprite;
    }

    function build() {
        clear();
        const {score, data, view, curves, dark, layout, pitchRange} = state;
        const {row, ruler, width} = layout, pixel = length / width;
        clip = flat ? [...sides, ceiling] : sides;
        renderer.setClearColor(dark ? "#171b20" : "#f5f7f9");
        if (!score) return;
        const depth = Math.max(spacing, score.tracks.length * spacing);
        origin = data?.from ?? view.from;
        const end = data?.to ?? view.to;
        const x = t => (Math.max(origin, Math.min(end, t)) - origin) / view.span * length - length / 2;
        const z = i => (i - (score.tracks.length - 1) / 2) * spacing;
        const sources = score.tracks.filter(t => t[0] >= 0).map(t => score.nodes[t[0]]);
        const base = Math.floor(Math.min(...sources.map(n => n.low), 48) / 12) * 12;
        const top = Math.max(...sources.map(n => n.high), base + 12);
        const pitchY = pitch => 1 + (pitch - base) * .25;
        const ranges = score.tracks.map(pitchRange);
        const rowPitch = (i, pitch) => {
            const [low, high] = ranges[i];
            return row - 8 - (pitch - low) * (row - 16) / (high - low + 1);
        };
        const ink = dark ? "#adb3ba" : "#59616a", grid = dark ? "#45515d" : "#bac7d0";
        const rawStep = Math.max(flat ? view.span * 85 / width : view.span / 8, (end - origin) / (flat ? 64 : 24));
        const power = 10 ** Math.floor(Math.log10(rawStep));
        const step = [1, 2, 5, 10].find(n => n * power >= rawStep) * power;
        const digits = Math.max(0, -Math.floor(Math.log10(step)));
        for (let j = Math.ceil(origin / step); j * step <= end; ++j) {
            const t = j * step;
            line(flat ? [[x(t), 0, 0], [x(t), -score.tracks.length * row * pixel, 0]] :
                [[x(t), 0, -depth / 2], [x(t), 0, depth / 2]], grid, .4);
            ticks.push(label(`${Number(t.toFixed(digits))}s`, x(t), 0, flat ? 0 : depth / 2 + 4, ink, content));
        }
        score.tracks.forEach((_, i) => {
            if (flat) line([[-length / 2, -(i + 1) * row * pixel, 0], [length / 2, -(i + 1) * row * pixel, 0]], grid, .6, world);
            else {
                line([[-length / 2, 0, z(i)], [length / 2, 0, z(i)]], color(i), .35, world);
                const title = label(String(i + 1), -length / 2 - 3, 0, z(i), color(i));
                title.userData.hit = {track: i, revision: score.revision}; targets.push(title);
            }
            const lane = data?.lanes[i - data.first];
            const events = lane?.notes || [];
            // Dense windows retain pitch extents and counts rather than inventing individual notes.
            const bins = lane?.density?.flatMap(([count, low, high], j, all) => {
                const a = data.from + (data.to - data.from) * j / all.length, b = a + (data.to - data.from) / all.length;
                return count ? [{a, b, low, high, count}] : [];
            }) || [];
            if (!events.length && !bins.length) return;
            const material = {color: color(i), transparent: true, clippingPlanes: clip};
            const mesh = new THREE.InstancedMesh(flat ? new THREE.PlaneGeometry(1, 1) : new THREE.BoxGeometry(1, 1, 1),
                flat ? new THREE.MeshBasicMaterial({...material, depthTest: false, depthWrite: false}) :
                    new THREE.MeshStandardMaterial({...material, roughness: .5, metalness: .1}), events.length + bins.length);
            if (flat) mesh.renderOrder = 1;
            const entries = [...events.map(note => ({a: note[1], b: note[2], low: note[3], high: note[3], note})), ...bins];
            entries.forEach((entry, index) => {
                const {a, b, low, high, note} = entry;
                if (flat) {
                    const height = Math.max(2, Math.min(9, (row - 16) / (ranges[i][1] - ranges[i][0] + 1)));
                    const bottom = rowPitch(i, low), top = rowPitch(i, high) - height;
                    transform.position.set((x(a) + x(b)) / 2, -(i * row + (top + bottom) / 2) * pixel, 0);
                    transform.scale.set(Math.max(pixel, x(b) - x(a)), (bottom - top) * pixel, 1);
                } else {
                    transform.position.set((x(a) + x(b)) / 2, (pitchY(low) + pitchY(high)) / 2, z(i));
                    transform.scale.set(Math.max(.12, x(b) - x(a)), Math.max(.4, pitchY(high) - pitchY(low)), .85);
                }
                transform.updateMatrix(); mesh.setMatrixAt(index, transform.matrix);
                mesh.setColorAt(index, tint.setScalar(note ? .45 + note[4] / 127 * .55 : .65));
            });
            mesh.userData.entries = entries; mesh.userData.track = i; mesh.userData.revision = data.revision;
            content.add(mesh); meshes.push(mesh); targets.push(mesh);
        });
        for (const {index, parameter: p, result} of curves) {
            const point = (t, value) => {
                const ratio = automationRatio(p.p, value);
                return flat ? [x(t), -(index * row + row - 8 - ratio * (row - 16)) * pixel, 0] :
                    [x(t), .6 + ratio * 5, z(index) + 1.6];
            };
            const points = result.points || [[result.from, result.initial], ...result.bins.map((bin, j, bins) =>
                [result.from + (result.to - result.from) * (j + 1) / bins.length, bin[2]])];
            const path = [];
            for (let j = 0; j < points.length; ++j) {
                const [a, value] = points[j], b = points[j + 1]?.[0] ?? result.to;
                if (b <= origin || a >= end) continue;
                path.push(point(a, value), point(b, value));
            }
            if (path.length) line(path, `hsl(${p.hue}, 65%, ${dark ? 70 : 38}%)`, flat ? .85 : .65).renderOrder = 2;
            if (result.bins) {
                const extents = [];
                result.bins.forEach(([low, high], j, bins) => {
                    const a = result.from + (result.to - result.from) * j / bins.length;
                    const b = a + (result.to - result.from) / bins.length;
                    if (b <= origin || a >= end) return;
                    const t = (a + b) / 2;
                    extents.push(new THREE.Vector3(...point(t, low)), new THREE.Vector3(...point(t, high)));
                });
                content.add(new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(extents),
                    new THREE.LineBasicMaterial({color: `hsl(${p.hue}, 65%, ${dark ? 70 : 38}%)`, transparent: true, opacity: .25, clippingPlanes: clip})));
            }
        }
        if (flat) {
            rulerBand = new THREE.Mesh(new THREE.PlaneGeometry(length, ruler * pixel), new THREE.MeshBasicMaterial({
                color: dark ? "#171b20" : "#f5f7f9", transparent: true, depthTest: false, depthWrite: false
            }));
            rulerBand.renderOrder = 3; world.add(rulerBand);
            head = line([[0, 0, 0], [0, -1, 0]], dark ? "#ffd377" : "#995b00", 1, world);
            head.material.depthTest = false; head.renderOrder = 5;
        } else {
            line([[-length / 2 - 6, 0, depth / 2], [-length / 2 - 6, pitchY(top), depth / 2]], grid, .6, world);
            for (let pitch = base; pitch <= top; pitch += 12)
                label(`C${pitch / 12 - 1}`, -length / 2 - 9, pitchY(pitch), depth / 2, ink);
            head = new THREE.Group();
            const headHeight = Math.max(10, pitchY(top) + 2);
            const plane = new THREE.Mesh(new THREE.PlaneGeometry(depth, headHeight), new THREE.MeshBasicMaterial({
                color: dark ? "#ffd377" : "#995b00", opacity: .06, transparent: true, side: THREE.DoubleSide, depthWrite: false
            }));
            plane.rotation.y = Math.PI / 2; plane.position.y = headHeight / 2; head.add(plane);
            line([[0, .1, -depth / 2], [0, .1, depth / 2]], dark ? "#ffd377" : "#995b00", 1, head);
            world.add(head);
        }
        if (reset) home();
    }

    function home() {
        const depth = Math.max(20, (state?.score?.tracks.length || 0) * spacing);
        const aspect = canvas.clientWidth / Math.max(1, canvas.clientHeight);
        const extent = Math.max(length + 12, depth + 15) * Math.max(1, 1 / Math.max(.25, aspect));
        controls.target.set(0, 0, depth * .09);
        camera.position.set(-extent * .56, extent * 1.12, extent * 1.17);
        camera.lookAt(controls.target); controls.update();
        reset = false;
        schedule();
    }

    function paint() {
        frame = 0;
        if (!visible || lost || !state) return;
        const width = canvas.clientWidth, height = canvas.clientHeight;
        if (!width || !height) return;
        const ratio = Math.min(devicePixelRatio, 2);
        if (canvas.width !== Math.round(width * ratio) || canvas.height !== Math.round(height * ratio)) {
            renderer.setPixelRatio(ratio); renderer.setSize(width, height, false);
            camera.aspect = width / height; camera.updateProjectionMatrix();
        }
        const {score, data, view, curves, dark, time, playing, selected, audible, layout} = state;
        const pixel = length / width;
        if (flat) {
            // Screen pixels and DOM track rows share one scale. Vertical scrolling
            // moves the orthographic camera, not the note geometry.
            plan.top = (layout.ruler - layout.scroll) * pixel;
            plan.bottom = plan.top - height * pixel;
            plan.updateProjectionMatrix();
            ceiling.constant = -layout.scroll * pixel;
        }
        reset ||= trackCount !== score?.tracks.length;
        trackCount = score?.tracks.length;
        if (!previous || previous.score !== score || previous.data !== data || previous.dark !== dark ||
            previous.view.span !== view.span ||
            flat && (previous.layout.row !== layout.row || previous.layout.width !== layout.width) ||
            previous.curves.length !== curves.length || curves.some((c, i) => c.result !== previous.curves[i].result || c.parameter !== previous.curves[i].parameter)) {
            build();
        }
        previous = state;
        // Geometry is anchored to the buffer. Follow moves it under a stationary
        // playhead; clipping keeps the prefetched edges outside the visible score.
        content.position.x = (origin - view.from) / view.span * length;
        for (const tick of ticks) {
            tick.visible = Math.abs(tick.position.x + content.position.x) <= length / 2;
            if (flat) tick.position.y = (layout.ruler / 2 - layout.scroll) * pixel;
        }
        if (rulerBand) rulerBand.position.y = (layout.ruler / 2 - layout.scroll) * pixel;
        let count = 0, active = 0;
        for (const mesh of meshes) {
            const track = mesh.userData.track;
            mesh.material.opacity = audible(track) ? 1 : .2;
            mesh.userData.entries.forEach(({note}, i) => {
                if (!note) return;
                if (note[2] > view.from && note[1] < view.to && (!flat || track >= view.first && track < view.first + view.count)) ++count;
                const sounding = playing && time >= note[1] && time < Math.max(note[2], note[1] + .1);
                if (sounding) ++active;
                const chosen = selected?.node === score.tracks[track][0] && selected.order === note[0] && selected.start === note[1];
                tint.setScalar(chosen || sounding ? 1.7 : .45 + note[4] / 127 * .55);
                mesh.setColorAt(i, tint);
            });
            mesh.instanceColor.needsUpdate = true;
        }
        if (head) {
            head.position.x = (time - view.from) / view.span * length - length / 2;
            head.visible = time >= view.from && time <= view.to;
            if (flat) { head.position.y = plan.top; head.scale.y = height * pixel; }
        }
        message.hidden = !!score;
        canvas.dataset.notes = count;
        canvas.dataset.projection = flat ? "2d" : "3d";
        canvas.dataset.active = active;
        canvas.dataset.dense = Boolean(data?.lanes.some(lane => lane.density));
        canvas.dataset.automation = curves.length;
        canvas.dataset.automationDense = curves.some(c => c.result.bins);
        canvas.dataset.revision = score?.revision || 0;
        canvas.dataset.from = view.from;
        canvas.dataset.to = view.to;
        canvas.dataset.bufferFrom = data?.from ?? view.from;
        canvas.dataset.bufferTo = data?.to ?? view.to;
        canvas.dataset.time = time;
        canvas.setAttribute("aria-label", `${flat ? "2D" : "Spatial"} score, ${view.from.toFixed(2)}–${view.to.toFixed(2)} seconds. ${score?.tracks.length || 0} tracks.`);
        renderer.render(scene, flat ? plan : camera);
    }

    function schedule() {
        if (visible && !lost && !frame) frame = requestAnimationFrame(() => {
            try { paint(); }
            catch (cause) { frame = 0; visible = false; controls.enabled = false; fail(cause); }
        });
    }

    function hit(event) {
        if (!visible || lost || !state) return;
        const rect = canvas.getBoundingClientRect();
        pointer.set((event.clientX - rect.left) / rect.width * 2 - 1, -(event.clientY - rect.top) / rect.height * 2 + 1);
        ray.setFromCamera(pointer, flat ? plan : camera);
        const found = ray.intersectObjects(targets).find(h => !h.object.isInstancedMesh ||
            Math.abs(h.point.x) <= length / 2 && (!flat || ceiling.distanceToPoint(h.point) >= 0));
        if (!found) return;
        if (found.instanceId === undefined) return found.object.userData.hit;
        const track = found.object.userData.track, entry = found.object.userData.entries[found.instanceId];
        return {track, revision: found.object.userData.revision, ...entry};
    }
    canvas.addEventListener("webglcontextlost", event => {
        event.preventDefault(); lost = true; controls.enabled = false;
        message.hidden = false; message.textContent = "Graphics context lost. Tracks remains available.";
    });
    canvas.addEventListener("webglcontextrestored", () => {
        lost = false; controls.enabled = visible && !flat; previous = undefined;
        message.textContent = "Run a score to see its notes"; schedule();
    });
    controls.addEventListener("change", schedule);
    const observer = new ResizeObserver(schedule); observer.observe(canvas);
    return {
        update(value) {
            if (flat !== value.flat) previous = undefined;
            state = value; flat = value.flat;
            controls.enabled = visible && !flat && !lost;
            schedule();
        },
        visible(value) {
            visible = value; controls.enabled = value && !flat && !lost;
            if (!value) { cancelAnimationFrame(frame); frame = 0; }
            else schedule();
        },
        hit,
        help,
        home,
        dispose() {
            visible = false; cancelAnimationFrame(frame); observer.disconnect(); controls.dispose(); clear(); renderer.dispose();
        }
    };
}
