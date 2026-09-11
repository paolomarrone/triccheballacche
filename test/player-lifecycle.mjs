import {preparePlayer, closePlayer} from "../web/player.js";

const check = (ok, message) => { if (!ok) throw Error(message); };
async function rejects(promise, pattern) {
    try { await promise; } catch (error) {
        check(pattern.test(String(error)), "Unexpected error: " + error);
        return;
    }
    throw Error("Expected failure: " + pattern);
}

export async function testPlayerLifecycle(host) {
    const freePlayer = host._player_free, freeScore = host._score_free;
    const NativeNode = globalThis.AudioWorkletNode;
    let players = 0, scores = 0, fault;
    host._player_free = pointer => {
        const context = host.emscriptenGetAudioObject(host._player_context(pointer));
        check(context.state === "closed", "C memory released before the audio context stopped");
        freePlayer(pointer);
        players++;
    };
    host._score_free = pointer => { freeScore(pointer); scores++; };
    globalThis.AudioWorkletNode = class extends NativeNode {
        constructor(context, name, options) {
            super(context, name, options);
            if (name === "perone-setup") {
                const post = this.port.postMessage.bind(this.port);
                this.port.postMessage = message => {
                    if (message.type === "close" && fault === "send") throw Error("injected send failure");
                    if (message.type === "close" && fault === "drop") return;
                    post(message);
                };
            }
        }
    };
    const open = (path = "test/playback.janet") => preparePlayer(host, path, 48000);
    const clean = count => {
        check(players === count && scores === count, "Resources were not released exactly once");
        check(host.perone.instances.size === 0 && host.perone.remote.size === 0, "DSP handles remain owned");
    };
    try {
        let player = await open();
        await player.context.close(); // A caller/browser can close the context before the player.
        await player.close();
        await player.close();
        clean(1);

        player = await open();
        await player.start();
        player.context.suspend = () => Promise.reject(Error("injected suspend failure"));
        await rejects(player.close(), /suspend failure/);
        clean(2); // Closing the context still makes it safe to release C memory.

        player = await open("test/schedule.janet");
        await player.start();
        const close = player.context.close.bind(player.context);
        player.context.close = () => Promise.reject(Error("injected context close failure"));
        await rejects(player.close(), /resources retained/);
        check(players === 2 && scores === 2, "Memory must remain valid until context closure is confirmed");
        await rejects(open(), /current player/);
        await rejects(player.start(), /closing or closed/);
        let processorFailed = false;
        player.node.addEventListener("processorerror", () => { processorFailed = true; });
        await player.context.resume(); // Even an external resume must not access already released DSPs.
        await new Promise(resolve => setTimeout(resolve, 20));
        check(!processorFailed, "The callback used a DSP after teardown began");
        player.context.close = close;
        await player.close();
        clean(3);

        for (const [mode, pattern] of [["send", /send failure/], ["drop", /timed out/]]) {
            player = await open();
            fault = mode;
            await rejects(player.close(), pattern);
            fault = null;
        }
        clean(5);

        // Cancellation waits for the asynchronous miniaudio initialization before touching C state.
        const preparing = rejects(open(), /preparation cancelled/);
        await closePlayer(host);
        await preparing;
        clean(6);

        // A failed preparation returns no player: the host-level close must allow a later retry.
        const getObject = host.emscriptenGetAudioObject, release = host.perone.releasePrepared;
        let failedContext, originalClose;
        host.emscriptenGetAudioObject = handle => {
            const object = getObject(handle);
            if (object instanceof AudioContext && !failedContext) {
                failedContext = object;
                originalClose = object.close.bind(object);
                object.close = () => Promise.reject(Error("injected context close failure"));
            }
            return object;
        };
        host.perone.releasePrepared = () => { throw Error("injected preparation failure"); };
        try {
            await rejects(open(), /preparation failure.*resources retained/);
            check(players === 6 && scores === 6, "Failed preparation released live memory");
        } finally {
            host.emscriptenGetAudioObject = getObject;
            host.perone.releasePrepared = release;
            if (failedContext) failedContext.close = originalClose;
        }
        await closePlayer(host);
        clean(7);
        player = await open();
        await player.close();
        clean(8);
    } finally {
        fault = null;
        try { await closePlayer(host); } finally {
            globalThis.AudioWorkletNode = NativeNode;
            host._player_free = freePlayer;
            host._score_free = freeScore;
        }
    }
}
