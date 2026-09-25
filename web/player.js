import createModule from "../build/web/player.mjs";
import {createHost} from "./host.js";
import {workletReply} from "./request.js";

const active = new WeakMap();

export function createPlayerHost(options = {}) {
    return createHost(options, createModule);
}

// Also permits retrying cleanup after a preparation failure returned no player.
export function closePlayer(host) {
    return active.get(host)?.() || Promise.resolve();
}

// Preparation owns descriptions only. The worklet creates the DSPs once, when attaching audio.
export function prepareScore(host, path, sampleRate, source) {
    host.perone.deferred = true;
    try {
        const score = source === undefined ? host.ccall("score_new", "number", ["string", "number"], [path, sampleRate]) :
            host.ccall("score_prepare", "number", ["string", "string", "number"], [path, source, sampleRate]);
        if (!score) throw Error("Score preparation failed; see Janet diagnostics");
        return score;
    } finally { host.perone.deferred = false; }
}

export async function preparePlayer(host, path, sampleRate, source) {
    if (active.has(host)) throw Error("Close the current player before preparing another score");
    return attachPlayer(host, prepareScore(host, path, sampleRate, source));
}

// Takes ownership of score, including on failure.
export async function attachPlayer(host, score) {
    if (active.has(host)) {
        host._score_free(score);
        throw Error("Close the current player before attaching another score");
    }
    let player = 0, context, setup, error, closing, prepared, stopping = false, released = false;
    const preparation = new Promise(resolve => { prepared = resolve; });
    const release = async () => {
        const errors = [];
        if (player) host._player_stop(player);
        if (context && context.state !== "closed") {
            try { await context.suspend(); } catch (error) { errors.push(error); }
            if (context.state === "suspended" && setup) {
                try { await workletReply(setup, "close", {send: true}); } catch (error) { errors.push(error); }
            }
            try { await context.close(); } catch (error) { errors.push(error); }
            if (context.state !== "closed") {
                // Retain memory and the host lock: an audio callback may still use them. close() can be retried.
                throw new AggregateError(errors, "Audio context is still open; player resources retained");
            }
        }
        setup?.port.close();
        try { if (player) host._player_free(player); } catch (error) { errors.push(error); }
        try { if (score) host._score_free(score); } catch (error) { errors.push(error); }
        released = true;
        active.delete(host);
        if (errors.length) throw new AggregateError(errors, "Player cleanup failed: " + errors.map(String).join("; "));
    };
    const close = () => {
        stopping = true;
        return closing ||= preparation.then(release).catch(error => {
            if (!released) closing = undefined;
            throw error;
        });
    };
    active.set(host, close);
    try {
        player = await host.ccall("score_player", "number", ["number"], [score], {async: true});
        if (!player) throw Error("Miniaudio initialization failed; see diagnostics");
        context = host.emscriptenGetAudioObject(host._player_context(player));
        if (stopping) throw Error("Player preparation cancelled");
        const node = host.emscriptenGetAudioObject(host._player_node(player));
        node.onprocessorerror = () => { error = Error("AudioWorklet processing failed"); };
        await context.suspend();
        await context.audioWorklet.addModule(new URL("./worklet.js", import.meta.url));
        setup = new AudioWorkletNode(context, "perone-setup", {
            numberOfInputs: 0, numberOfOutputs: 1, processorOptions: host.perone.releasePrepared()
        });
        await workletReply(setup, "ready");
        if (stopping) throw Error("Player preparation cancelled");
        prepared();
        return {
            context, node,
            cancel() { host._score_cancel(score); },
            activateView(view) { host._score_view_activate_web(score, view); },
            get live() { return !!host._score_live(score); },
            get revision() { return host._score_revision(score); },
            update(next, revision) {
                if (stopping) throw Error("Player closing or closed");
                const error = host._player_update_score(player, next, revision);
                if (error) throw Error(host.UTF8ToString(error));
            },
            listen(track, flags) {
                if (stopping) throw Error("Player closing or closed");
                if (!Number.isInteger(track) || !Number.isInteger(flags) || track < 0 || track >= 32 || flags < 0 || flags > 3 ||
                    host._score_listen(score, track, flags)) throw Error("Invalid track state");
            },
            control(op, id, ...args) {
                if (stopping) throw Error("Player closing or closed");
                const dsp = host._score_dsp(score, id);
                if (!dsp) throw Error("Invalid plugin node");
                return workletReply(setup, "control", {send: true, payload: {player, args: [op, dsp, ...args]}});
            },
            // Transfer only the immutable projection; closing this player still owns all audio resources.
            takeView() {
                if (stopping) throw Error("Player closing or closed");
                return host._score_take_view(score);
            },
            get time() {
                if (stopping) throw Error("Player closing or closed");
                return host._player_time(player);
            },
            get status() {
                if (stopping) throw Error("Player closing or closed");
                if (error) throw error;
                return host._player_status(player);
            },
            async start() {
                if (stopping) throw Error("Player closing or closed");
                if (host._player_start(player)) throw Error("Miniaudio playback failed");
                await context.resume();
            },
            async stop() {
                if (stopping) throw Error("Player closing or closed");
                if (host._player_pause(player)) throw Error("Cannot stop audio device");
                await context.suspend();
            },
            async restart() {
                if (stopping) throw Error("Player closing or closed");
                if (host._player_pause(player)) throw Error("Cannot stop audio device");
                await context.suspend();
                if (stopping) throw Error("Player closing or closed");
                await workletReply(setup, "rewind", {send: true, payload: {player}});
                if (stopping) throw Error("Player closing or closed");
                if (host._player_start(player)) throw Error("Miniaudio playback failed");
                await context.resume();
            },
            close
        };
    } catch (error) {
        prepared();
        try { await close(); } catch (cleanup) {
            throw new AggregateError([error, cleanup], "Player preparation failed: " + error + "; " + cleanup);
        }
        throw error;
    }
}
