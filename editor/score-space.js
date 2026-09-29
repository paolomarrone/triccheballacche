import * as THREE from "three";
import {OrbitControls} from "./vendor/three/OrbitControls.js";
import {automationRatio} from "./automation.js";

// A renderer of the shared viewport. It owns no score queries, transport or audition state.
export function scoreSpace(canvas, message, pick, fail) {
    const renderer = new THREE.WebGLRenderer({canvas, antialias: true});
    const scene = new THREE.Scene(), camera = new THREE.PerspectiveCamera(38, 1, 0.1, 2000);
    scene.add(new THREE.HemisphereLight("#ffffff", "#455166", 2));
    const light = new THREE.DirectionalLight("#ffffff", 2);
    light.position.set(-30, 80, 40); scene.add(light);
    const controls = new OrbitControls(camera, canvas);
    controls.maxPolarAngle = Math.PI * 0.49;
    controls.minDistance = 8;
    controls.maxDistance = 800;
    const ray = new THREE.Raycaster(), pointer = new THREE.Vector2(), transform = new THREE.Object3D(), tint = new THREE.Color();
    let world = new THREE.Group(), content = new THREE.Group(), head;
    let meshes = [], targets = [], ticks = [], state, previous, visible = false, flat = false;
    let frame = 0, lost = false, reset = true, down, origin = 0;
    const length = 80, spacing = 5;
    const clip = [new THREE.Plane(new THREE.Vector3(1, 0, 0), length / 2),
        new THREE.Plane(new THREE.Vector3(-1, 0, 0), length / 2)];
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
        meshes = []; targets = []; ticks = []; head = undefined;
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
        sprite.position.set(x, y, z); sprite.scale.set(image.width / image.height * 3, 3, 1);
        parent.add(sprite);
        return sprite;
    }

    function build() {
        clear();
        const {score, data, view, curves, dark} = state;
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
        const ink = dark ? "#adb3ba" : "#59616a", grid = dark ? "#45515d" : "#bac7d0";
        const rawStep = Math.max(view.span / 8, (end - origin) / 24);
        const power = 10 ** Math.floor(Math.log10(rawStep));
        const step = [1, 2, 5, 10].find(n => n * power >= rawStep) * power;
        const digits = Math.max(0, -Math.floor(Math.log10(step)));
        for (let j = Math.ceil(origin / step); j * step <= end; ++j) {
            const t = j * step;
            line([[x(t), 0, -depth / 2], [x(t), 0, depth / 2]], grid, .4);
            ticks.push(label(`${Number(t.toFixed(digits))}s`, x(t), 0, depth / 2 + 4, ink, content));
        }
        score.tracks.forEach((_, i) => {
            line([[-length / 2, 0, z(i)], [length / 2, 0, z(i)]], color(i), .35, world);
            const title = label(String(i + 1), -length / 2 - 3, 0, z(i), color(i));
            title.userData.hit = {track: i, revision: score.revision}; targets.push(title);
            const lane = data?.lanes[i - data.first];
            const events = lane?.notes || [];
            // Dense windows retain pitch extents and counts rather than inventing individual notes.
            const bins = lane?.density?.flatMap(([count, low, high], j, all) => {
                const a = data.from + (data.to - data.from) * j / all.length, b = a + (data.to - data.from) / all.length;
                return count ? [{a, b, low, high, count}] : [];
            }) || [];
            if (!events.length && !bins.length) return;
            const mesh = new THREE.InstancedMesh(new THREE.BoxGeometry(1, 1, 1),
                new THREE.MeshStandardMaterial({color: color(i), transparent: true, roughness: .5, metalness: .1, clippingPlanes: clip}), events.length + bins.length);
            const entries = [...events.map(note => ({a: note[1], b: note[2], low: note[3], high: note[3], note})), ...bins];
            entries.forEach((entry, index) => {
                const {a, b, low, high, note} = entry, pitch = (low + high) / 2;
                transform.position.set((x(a) + x(b)) / 2, flat ? .4 : (pitchY(low) + pitchY(high)) / 2,
                    z(i) + (flat ? (pitch - 64) / 127 * 3.2 : 0));
                transform.scale.set(Math.max(.12, x(b) - x(a)), flat ? .3 : Math.max(.4, pitchY(high) - pitchY(low)), .85);
                transform.updateMatrix(); mesh.setMatrixAt(index, transform.matrix);
                mesh.setColorAt(index, tint.setScalar(note ? .45 + note[4] / 127 * .55 : .65));
            });
            mesh.userData.entries = entries; mesh.userData.track = i; mesh.userData.revision = data.revision;
            content.add(mesh); meshes.push(mesh); targets.push(mesh);
        });
        for (const {index, parameter: p, result} of curves) {
            const y = value => .6 + automationRatio(p.p, value) * (flat ? 2 : 5);
            const points = result.points || [[result.from, result.initial], ...result.bins.map((bin, j, bins) =>
                [result.from + (result.to - result.from) * (j + 1) / bins.length, bin[2]])];
            const path = [];
            for (let j = 0; j < points.length; ++j) {
                const [a, value] = points[j], b = points[j + 1]?.[0] ?? result.to;
                if (b <= origin || a >= end) continue;
                path.push([x(a), flat ? .6 : y(value), z(index) + (flat ? y(value) : 1.6)],
                    [x(b), flat ? .6 : y(value), z(index) + (flat ? y(value) : 1.6)]);
            }
            if (path.length) line(path, `hsl(${p.hue}, 65%, ${dark ? 70 : 38}%)`, .65);
            if (result.bins) {
                const extents = [];
                result.bins.forEach(([low, high], j, bins) => {
                    const a = result.from + (result.to - result.from) * j / bins.length;
                    const b = a + (result.to - result.from) / bins.length;
                    if (b <= origin || a >= end) return;
                    const t = (x(a) + x(b)) / 2;
                    extents.push(new THREE.Vector3(t, flat ? .6 : y(low), z(index) + (flat ? y(low) : 1.6)),
                        new THREE.Vector3(t, flat ? .6 : y(high), z(index) + (flat ? y(high) : 1.6)));
                });
                content.add(new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(extents),
                    new THREE.LineBasicMaterial({color: `hsl(${p.hue}, 65%, ${dark ? 70 : 38}%)`, transparent: true, opacity: .25, clippingPlanes: clip})));
            }
        }
        if (!flat) {
            line([[-length / 2 - 6, 0, depth / 2], [-length / 2 - 6, pitchY(top), depth / 2]], grid, .6, world);
            for (let pitch = base; pitch <= top; pitch += 12)
                label(`C${pitch / 12 - 1}`, -length / 2 - 9, pitchY(pitch), depth / 2, ink);
        }
        head = new THREE.Group();
        const headHeight = Math.max(10, pitchY(top) + 2);
        const plane = new THREE.Mesh(new THREE.PlaneGeometry(depth, headHeight), new THREE.MeshBasicMaterial({
            color: dark ? "#ffd377" : "#995b00", opacity: .06, transparent: true, side: THREE.DoubleSide, depthWrite: false
        }));
        plane.rotation.y = Math.PI / 2; plane.position.y = headHeight / 2; head.add(plane);
        line([[0, .1, -depth / 2], [0, .1, depth / 2]], dark ? "#ffd377" : "#995b00", 1, head);
        world.add(head);
        if (reset) home();
    }

    function home() {
        const depth = Math.max(20, (state?.score?.tracks.length || 0) * spacing);
        const aspect = canvas.clientWidth / Math.max(1, canvas.clientHeight);
        const extent = Math.max(length + 12, depth + 15) * Math.max(1, 1 / Math.max(.25, aspect));
        controls.target.set(0, 0, depth * .09);
        camera.position.set(flat ? 0 : -extent * .56, extent * (flat ? 1.65 : 1.12), flat ? controls.target.z + .01 : extent * 1.17);
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
        const {score, data, view, curves, dark, time, playing, selected, audible} = state;
        if (!previous || previous.score !== score || previous.data !== data || previous.dark !== dark ||
            previous.view.span !== view.span ||
            previous.curves.length !== curves.length || curves.some((c, i) => c.result !== previous.curves[i].result || c.parameter !== previous.curves[i].parameter)) {
            reset ||= previous?.score?.tracks.length !== score?.tracks.length;
            build();
        }
        previous = state;
        // Geometry is anchored to the buffer. Follow moves it under a stationary
        // playhead; clipping keeps the prefetched edges outside the visible score.
        content.position.x = (origin - view.from) / view.span * length;
        for (const tick of ticks) tick.visible = Math.abs(tick.position.x + content.position.x) <= length / 2;
        let count = 0, active = 0;
        for (const mesh of meshes) {
            const track = mesh.userData.track;
            mesh.material.opacity = audible(track) ? 1 : .2;
            mesh.userData.entries.forEach(({note}, i) => {
                if (!note) return;
                if (note[2] > view.from && note[1] < view.to) ++count;
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
        }
        message.hidden = !!score;
        canvas.dataset.notes = count;
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
        canvas.setAttribute("aria-label", `Spatial score, ${view.from.toFixed(2)}–${view.to.toFixed(2)} seconds. ${score?.tracks.length || 0} tracks. ${help}`);
        renderer.render(scene, camera);
    }

    function schedule() {
        if (visible && !lost && !frame) frame = requestAnimationFrame(() => {
            try { paint(); }
            catch (cause) { frame = 0; visible = false; controls.enabled = false; fail(cause); }
        });
    }

    function hit(event) {
        const rect = canvas.getBoundingClientRect();
        pointer.set((event.clientX - rect.left) / rect.width * 2 - 1, -(event.clientY - rect.top) / rect.height * 2 + 1);
        ray.setFromCamera(pointer, camera);
        const found = ray.intersectObjects(targets).find(h => !h.object.isInstancedMesh || Math.abs(h.point.x) <= length / 2);
        if (!found) return;
        if (found.instanceId === undefined) return found.object.userData.hit;
        const track = found.object.userData.track, entry = found.object.userData.entries[found.instanceId];
        return {track, revision: found.object.userData.revision, ...entry};
    }
    canvas.addEventListener("pointerdown", event => { down = {x: event.clientX, y: event.clientY, button: event.button}; });
    canvas.addEventListener("pointermove", event => {
        if (!visible || lost) return;
        if (down && Math.hypot(event.clientX - down.x, event.clientY - down.y) > 4) down = undefined;
        const found = hit(event);
        canvas.title = found?.note ? `Track ${found.track + 1} · MIDI ${found.note[3]} · ${found.a.toFixed(3)}–${found.b.toFixed(3)} s` :
            found?.count ? `${found.count} notes · MIDI ${found.low}–${found.high} · zoom in for individual notes` :
            found ? `Track ${found.track + 1} · click to open plugins` : help;
    });
    canvas.addEventListener("pointerup", event => {
        if (down && !down.button && visible && !lost) { const found = hit(event); if (found) pick(found); }
        down = undefined;
    });
    canvas.addEventListener("pointercancel", () => { down = undefined; });
    canvas.addEventListener("webglcontextlost", event => {
        event.preventDefault(); lost = true; controls.enabled = false;
        message.hidden = false; message.textContent = "Graphics context lost. Tracks remains available.";
    });
    canvas.addEventListener("webglcontextrestored", () => {
        lost = false; controls.enabled = visible; previous = undefined;
        message.textContent = "Run a score to see its notes"; schedule();
    });
    controls.addEventListener("change", schedule);
    const observer = new ResizeObserver(schedule); observer.observe(canvas);
    return {
        update(value) { state = value; schedule(); },
        visible(value) {
            visible = value; controls.enabled = value && !lost;
            if (!value) { cancelAnimationFrame(frame); frame = 0; down = undefined; }
            else schedule();
        },
        flat(value) { flat = value; controls.enableRotate = !value; reset = true; previous = undefined; schedule(); },
        home,
        dispose() {
            visible = false; cancelAnimationFrame(frame); observer.disconnect(); controls.dispose(); clear(); renderer.dispose();
        }
    };
}
