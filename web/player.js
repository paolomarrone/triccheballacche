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
        host._web_score_free(score);
        throw Error("Close the current player before attaching another score");
    }
    let midiBuffer = 0;
    let player = 0, transport = 0, context, setup, error, closing, prepared, stopping = false, released = false;
    const preparation = new Promise(resolve => { prepared = resolve; });
    const release = async () => {
        const errors = [];
        if (player) host._player_stop(player);
        if (context && context.state !== "closed") {
            try { await context.suspend(); } catch (error) { errors.push(error); }
            if (context.state === "suspended") {
                if (transport) host._transport_stopped(transport);
                if (setup) {
                    try { await workletReply(setup, "close", {send: true}); } catch (error) { errors.push(error); }
                }
            }
            try { await context.close(); } catch (error) { errors.push(error); }
            if (context.state !== "closed") {
                // Retain memory and the host lock: an audio callback may still use them. close() can be retried.
                throw new AggregateError(errors, "Audio context is still open; player resources retained");
            }
        }
        setup?.port.close();
        if (transport) {
            host._transport_stopped(transport);
            host._transport_detach(transport);
        }
        if (midiBuffer) host._free(midiBuffer);
        try { if (player) host._player_free(player); } catch (error) { errors.push(error); }
        try { if (score) host._web_score_free(score); } catch (error) { errors.push(error); }
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
        midiBuffer = host._malloc(1040);
        if (!midiBuffer) throw Error("Out of memory for MIDI");
        prepared();
        return {
            context, node,
            publish(state) {
                if (!state || stopping || transport) throw Error("Invalid transport or player already published/closed");
                if (host._transport_attach_web(state, score)) throw Error("Score projection missing");
                transport = state;
                if (host._player_status(player) === 0) host._transport_started(transport);
            },
            get duration() {
                if (stopping) throw Error("Player closing or closed");
                return host._score_duration(score);
            },
            canSeek(seconds) {
                if (stopping) throw Error("Player closing or closed");
                return Number.isFinite(seconds) && !!host._score_can_seek(score, seconds);
            },
            update(next) {
                if (stopping || !transport) throw Error("Player is closed or not published");
                const error = host._transport_update_web(transport, player, next);
                if (error) throw Error(host.UTF8ToString(error));
            },
            midi(node, bytes) {
                if (stopping || !bytes.length || bytes.length > 1024) throw Error("Invalid MIDI message");
                host.HEAPU8.set(bytes, midiBuffer);
                if (host._player_midi(player, node, midiBuffer, bytes.length)) throw Error("MIDI input rejected or overflowed");
            },
            captureMidi(node, enabled) {
                if (stopping || host._player_midi_capture(player, node, enabled)) throw Error("Invalid MIDI output");
            },
            readMidi() {
                if (stopping) return [];
                const packets = [];
                for (let i = 0; i < 256; ++i) {
                    const size = host._player_midi_read(player, midiBuffer);
                    if (!size) break;
                    const h = host.HEAPU32, p = midiBuffer >>> 2;
                    packets.push({node: h[p], time: (h[p + 1] + h[p + 2] * 4294967296) / context.sampleRate,
                        data: host.HEAPU8.slice(midiBuffer + 12, midiBuffer + 12 + size)});
                }
                return packets;
            },
            listen(track, flags) {
                if (stopping) throw Error("Player closing or closed");
                if (!Number.isInteger(track) || !Number.isInteger(flags) || track < 0 || track >= 32 || flags < 0 || flags > 3 ||
                    host._score_listen(score, track, flags)) throw Error("Invalid track state");
            },
            controls(ids) {
                if (stopping) throw Error("Player closing or closed");
                const dsps = ids.map(id => {
                    if (!Number.isInteger(id) || id < 0 || id > 0x7fffffff) throw Error("Invalid plugin node");
                    const dsp = host._score_dsp(score, id);
                    if (!dsp) throw Error("Invalid plugin node");
                    return dsp;
                });
                return workletReply(setup, "controls", {send: true, payload: {dsps}});
            },
            control(op, id, ...args) {
                if (stopping) throw Error("Player closing or closed");
                const dsp = host._score_dsp(score, id);
                if (!dsp) throw Error("Invalid plugin node");
                return workletReply(setup, "control", {send: true, payload: {player, args: [op, dsp, ...args]}});
            },
            state(id, data) {
                if (stopping || context.state !== "suspended") throw Error("Stop audio before accessing plugin state");
                if (!Number.isInteger(id) || !host._score_dsp(score, id)) throw Error("Invalid plugin node");
                return workletReply(setup, "state", {send: true, payload: {score, node: id, data}});
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
            get active() { return !stopping && !!host._player_active(player); },
            get audioTime() {
                if (stopping) throw Error("Player closing or closed");
                return host._player_audio_time(player);
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
                if (stopping) throw Error("Player closing or closed");
                if (transport) host._transport_started(transport);
            },
            async monitor() {
                if (stopping) throw Error("Player closing or closed");
                if (host._player_monitor(player)) throw Error("Cannot start audio monitoring");
                await context.resume();
                if (stopping) throw Error("Player closing or closed");
            },
            async stop() {
                const active = this.active;
                await this.pause(false);
                if (active) await this.monitor();
            },
            async pause(panic = true) {
                if (stopping) throw Error("Player closing or closed");
                if (host._player_pause(player)) throw Error("Cannot stop audio device");
                await context.suspend();
                if (stopping) throw Error("Player closing or closed");
                if (panic) host._player_midi_panic(player);
                if (transport) host._transport_stopped(transport);
            },
            async seek(seconds) {
                if (stopping) throw Error("Player closing or closed");
                if (!Number.isFinite(seconds)) throw Error("Invalid position");
                if (context.state !== "suspended") throw Error("Pause audio before seeking");
                await workletReply(setup, "seek", {send: true, payload: {player, seconds}});
                if (stopping) throw Error("Player closing or closed");
                if (transport) host._transport_seeked(transport);
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
