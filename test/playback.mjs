import {preparePlayer} from "../web/player.js";
import {renderScore, addFile} from "../web/host.js";

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const check = (ok, message) => { if (!ok) throw Error(message); };

// Compare playback with the session mix, before offline export normalization.
function renderMix(host, path, rate) {
    const score = host.ccall("score_new", "number", ["string", "number"], [path, rate]);
    check(score, "Reference preparation failed");
    try {
        const audio = new Float32Array(host._score_frames(score) * 2), pointer = host._score_buffer(score) / 4;
        for (let position = 0; position < audio.length;) {
            const n = host._score_render(score);
            check(n > 0, "Reference render failed");
            audio.set(host.HEAPF32.subarray(pointer, pointer + n * 2), position);
            position += n * 2;
        }
        return audio;
    } finally { host._score_free(score); }
}

export async function testPlayback(host, reference, path, rate, replay = false) {
    const expected = renderMix(reference, path, rate);
    const player = await preparePlayer(host, path, rate);
    try {
        check(player.duration === expected.length / (2 * rate), "Duration must use the rendered sample count");
        check(player.canSeek(player.duration) && !player.canSeek(player.duration + 1 / rate), "Invalid seek boundary");
        check(host.perone.instances.size === 0, "DSP instances must move to the worklet");
        const busy = await preparePlayer(host, path, rate).then(() => false, () => true);
        check(busy, "Concurrent players must be rejected");
        await player.context.audioWorklet.addModule(new URL("./capture.js", import.meta.url));
        for (let pass = 0; pass < (replay ? 3 : 1); ++pass) {
            const capture = new AudioWorkletNode(player.context, "capture", {
                outputChannelCount: [2], processorOptions: {frames: expected.length / 2}
            });
            const gain = player.context.createGain();
            gain.gain.value = 0.1;
            player.node.disconnect();
            player.node.connect(capture).connect(gain).connect(player.context.destination);
            if (pass) await player.seek(0);
            await player.start();
            if (pass === 1) {
                await sleep(20); // Stop with notes and effect history still active.
            } else {
                const deadline = performance.now() + expected.length / (2 * rate) * 1000 + 5000;
                while (!player.status && performance.now() < deadline) await sleep(50);
                check(player.status === 1, "Playback did not finish");
            }
            await player.stop();
            check(player.status === 2 && player.context.state === "suspended", "Stop must retain a suspended player");
            const time = player.time;
            await sleep(30);
            check(player.time === time, "Stopped transport advanced");
            const received = new Promise(resolve => { capture.port.onmessage = ({data}) => resolve(data); });
            capture.port.postMessage("read");
            const {audio, frames, reused} = await received;
            capture.port.close();
            capture.disconnect();
            gain.disconnect();
            check(reused, "Playback replaced worklet DSP instances");
            if (pass === 1) continue;
            check(frames * 2 > expected.length, "Missing audio or final silence");
            for (let i = 0; i < expected.length; i++)
                check(audio[i] === expected[i], `${path}, ${rate} Hz, pass ${pass}: PCM differs at sample ${i}: ${audio[i]} != ${expected[i]}`);
            for (let i = expected.length; i < frames * 2; i++) check(audio[i] === 0, "Final buffer must be padded with silence");
        }
    } finally {
        await player.close();
        await player.close();
    }
    check(player.context.state === "closed" && host.perone.instances.size === 0 && host.perone.remote.size === 0,
        "Incomplete cleanup");
}

export async function testPlayerExportOptions(host, reference) {
    const path = "test/export-options.janet";
    const source = '(def s (daw/plugin "build/test/fixture.perone" {:gain 0.25})) (daw/output (daw/track s)) ';
    const configured = new TextEncoder().encode(source + '(daw/end 0.05003 {:format :pcm16 :normalize 0.9})');
    await addFile(host, path, configured);
    await addFile(reference, path, configured);
    check(renderScore(reference, path, 48000)[0] === Math.fround(0.9), "Offline normalization must still apply");
    await testPlayback(host, reference, path, 48000, true);
}

export async function testSeeking(host, reference) {
    const path = "test/seek.janet", rate = 48000, frames = 4096;
    const source = '(import ../lib/pattern :as p) ' +
        '(def tone (daw/plugin :tone "build/test/fixture.perone")) ' +
        '(def fx (daw/plugin :fx "build/test/effect.perone" {:gain 0.5})) ' +
        '(def track (daw/track tone {:effects [fx]})) (daw/output track) (daw/tempo 60) ' +
        '(daw/score (p/loop (p/events 0.5 [[0 0.2 [:note tone 60 100]] [0.3 0.5 [:note tone 64 90]] ' +
        '[0 0 [:param tone :gain 0.25]] [0.1 0.1 [:param tone :gain 0.5]] ' +
        '[0 0 [:param track :pan 0]] [0.175 0.175 [:param track :pan -0.5]]]))';
    await addFile(host, path, new TextEncoder().encode(source + ')'));
    await addFile(reference, path, new TextEncoder().encode(source + ' {:duration 1})'));
    const expected = renderMix(reference, path, rate);
    const player = await preparePlayer(host, path, rate);
    try {
        check(player.duration === Infinity && player.canSeek(1000000.125), "Unbounded duration or seek limit");
        await player.context.audioWorklet.addModule(new URL("./capture.js", import.meta.url));
        for (const target of [0.125, 1000000.125, 0.3]) {
            await player.stop();
            await player.seek(target);
            check(player.time === target && player.status === 2, "Seek must publish its target and remain stopped");
            const capture = new AudioWorkletNode(player.context, "capture", {outputChannelCount: [2], processorOptions: {frames}});
            const gain = player.context.createGain();
            gain.gain.value = .01;
            player.node.disconnect();
            player.node.connect(capture).connect(gain).connect(player.context.destination);
            await player.start();
            for (const invalid of [-1, NaN, Infinity, "1", null])
                check(!player.canSeek(invalid), "Invalid seek accepted");
            check(player.status === 0, "Position validation interrupted playback");
            const deadline = performance.now() + 3000;
            while (player.time < target + frames / rate && performance.now() < deadline) await sleep(20);
            await player.stop();
            const received = new Promise(resolve => { capture.port.onmessage = ({data}) => resolve(data); });
            capture.port.postMessage("read");
            const result = await received;
            capture.port.close(); capture.disconnect(); gain.disconnect();
            check(result.reused && result.frames >= frames, "Seek replaced DSPs or failed to resume audio");
            const offset = Math.round((target % .5) * rate) * 2;
            for (let i = 0; i < frames * 2; ++i)
                check(result.audio[i] === expected[offset + i], `Seek to ${target}: PCM differs at sample ${i}`);
        }
    } finally { await player.close(); }
}

export async function testListening(host) {
    await addFile(host, "test/listening.janet", new TextEncoder().encode(
        '(def a (daw/track (daw/plugin "build/test/fixture.perone" {:gain 0.25}))) ' +
        '(def b (daw/track (daw/plugin "build/test/fixture.perone" {:gain 0.5}))) (daw/output (daw/mix [a b])) (daw/end 0.3)'));
    const player = await preparePlayer(host, "test/listening.janet", 48000);
    try {
        await player.context.audioWorklet.addModule(new URL("./capture.js", import.meta.url));
        for (const [pass, [a, b, expected]] of [[1, 0, .5], [2, 2, .75], [2, 0, .25], [3, 2, .5]].entries()) {
            player.listen(0, a);
            player.listen(1, b);
            const capture = new AudioWorkletNode(player.context, "capture", {
                outputChannelCount: [2], processorOptions: {frames: 14400}
            });
            const gain = player.context.createGain();
            gain.gain.value = 0.01;
            player.node.disconnect();
            player.node.connect(capture).connect(gain).connect(player.context.destination);
            if (pass) await player.seek(0);
            await player.start();
            if (pass === 3) {
                await sleep(50);
                player.listen(1, 3); // Deliver the mute from the main thread during worklet rendering.
            }
            const deadline = performance.now() + 3000;
            while (!player.status && performance.now() < deadline) await sleep(20);
            check(player.status === 1, "Track audition playback did not finish");
            await player.stop();
            const received = new Promise(resolve => { capture.port.onmessage = ({data}) => resolve(data); });
            capture.port.postMessage("read");
            const {audio, reused} = await received;
            capture.port.close(); capture.disconnect(); gain.disconnect();
            check(reused, "Mute/solo replaced a DSP instance");
            check(audio[0] === expected && audio[1] === -expected, "Restart lost mute/solo or leaked its first sample");
            if (pass < 3) {
                for (let i = 0; i < 14400; ++i)
                    check(audio[2 * i] === expected && audio[2 * i + 1] === -expected, "Wrong solo/mute mix");
            } else {
                let fading = false;
                for (let i = 1; i < 14400; ++i) {
                    const previous = audio[2 * i - 2], value = audio[2 * i];
                    check(value >= 0 && value <= previous && previous - value < .0021, "Live mute clicked or reversed");
                    check(audio[2 * i + 1] === -value, "Stereo mute diverged");
                    fading ||= value > 0 && value < expected;
                }
                check(fading && audio[28798] === 0, "Live mute did not reach silence");
            }
        }
    } finally { await player.close(); }
}

export async function testPlayerErrors(host) {
    for (const source of ['(error "intentional Janet error")',
        '(daw/plugin "build/test/fixture.perone") (daw/end 1)']) {
        await addFile(host, "test/failure.janet", new TextEncoder().encode(source));
        const failed = await preparePlayer(host, "test/failure.janet", 48000).then(() => false, () => true);
        check(failed && host.perone.instances.size === 0, "Preparation failure leaked instances");
    }
    // Fail inside the worklet after its first DSP has already been initialized.
    const releasePrepared = host.perone.releasePrepared;
    host.perone.releasePrepared = function () {
        const state = releasePrepared.call(this);
        state.modules = state.modules.map(([path, module]) => [path, path.includes("effect.perone")
            ? new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0])) : module]);
        return state;
    };
    try {
        const failed = await preparePlayer(host, "test/playback.janet", 48000).then(() => false, () => true);
        check(failed && host.perone.instances.size === 0, "Worklet preparation failure leaked instances");
    } finally {
        host.perone.releasePrepared = releasePrepared;
    }
    const player = await preparePlayer(host, "test/schedule.janet", 48000);
    await player.start();
    await sleep(20);
    check(player.status === 0, "Cancellation test finished too early");
    await player.close();
    check(player.context.state === "closed", "Cancellation did not close the context");
}
