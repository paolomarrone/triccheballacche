import {preparePlayer} from "../web/player.js";
import {renderScore, addFile} from "../web/host.js";

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const check = (ok, message) => { if (!ok) throw Error(message); };

export async function testPlayback(host, reference, path, rate) {
    const expected = renderScore(reference, path, rate);
    const player = await preparePlayer(host, path, rate);
    try {
        check(host.perone.instances.size === 0, "DSP instances must move to the worklet");
        const busy = await preparePlayer(host, path, rate).then(() => false, () => true);
        check(busy, "Concurrent players must be rejected");
        await player.context.audioWorklet.addModule(new URL("./capture.js", import.meta.url));
        const capture = new AudioWorkletNode(player.context, "capture", {
            outputChannelCount: [2], processorOptions: {frames: expected.length / 2}
        });
        const gain = player.context.createGain();
        gain.gain.value = 0.1;
        player.node.disconnect();
        player.node.connect(capture).connect(gain).connect(player.context.destination);
        await player.start();
        for (let i = 0; !player.status && i < 200; i++) await sleep(50);
        check(player.status === 1, "Playback did not finish");
        await sleep(100);
        const received = new Promise(resolve => { capture.port.onmessage = ({data}) => resolve(data); });
        capture.port.postMessage("read");
        const {audio, frames} = await received;
        capture.port.close();
        check(frames * 2 > expected.length, "Missing audio or final silence");
        for (let i = 0; i < expected.length; i++)
            check(audio[i] === expected[i], `${path}, ${rate} Hz: PCM differs at sample ${i}: ${audio[i]} != ${expected[i]}`);
        for (let i = expected.length; i < frames * 2; i++) check(audio[i] === 0, "Final buffer must be padded with silence");
    } finally {
        await player.close();
        await player.close();
    }
    check(player.context.state === "closed" && host.perone.instances.size === 0 && host.perone.remote.size === 0,
        "Incomplete cleanup");
}

export async function testPlayerErrors(host) {
    for (const source of ['(error "intentional Janet error")',
        '(def s (daw/plugin "build/fixture.perone")) (daw/track s) (daw/end 1 {:normalize 0.9})']) {
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
