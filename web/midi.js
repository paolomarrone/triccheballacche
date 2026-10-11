// The browser owns device permissions and ports. Audio only sees the common C queues.
export function midiDevices(getPlayer, fail) {
    let access, timer;
    const routes = new Map();
    const key = port => `${port.type}:${port.id}`;
    function panic(port) {
        port.clear();
        for (let channel = 0; channel < 16; ++channel)
            for (const cc of [64, 120, 123]) port.send([0xb0 | channel, cc, 0]);
    }
    function stop() {
        clearInterval(timer);
        timer = undefined;
        let failure;
        for (const port of access?.outputs.values() || []) {
            if (!routes.has(key(port)) || port.state !== "connected") continue;
            try { panic(port); }
            catch (error) { failure ||= error; }
        }
        if (failure) throw failure;
    }
    function failed(error) {
        try { stop(); }
        catch (cleanup) { error = new AggregateError([error, cleanup], `${error}; ${cleanup}`); }
        fail(error);
    }
    function start() {
        if (timer || !access) return;
        timer = setInterval(() => {
            try {
                const player = getPlayer();
                if (!player || player.status !== 0) return;
                for (const packet of player.readMidi()) {
                    const latency = player.context.baseLatency + (player.context.outputLatency || 0);
                    const at = performance.now() + Math.max(0, packet.time - player.time + latency) * 1000;
                    for (const port of access.outputs.values())
                        if (routes.get(key(port)) === packet.node && port.state === "connected") port.send(packet.data, at);
                }
            } catch (error) { failed(error); }
        }, 2);
    }
    async function list() {
        if (!navigator.requestMIDIAccess) throw Error("This browser does not support Web MIDI");
        access ||= await navigator.requestMIDIAccess({sysex: true});
        access.onstatechange = event => {
            if (event.port.state !== "disconnected" || !routes.has(key(event.port))) return;
            // Stop also releases notes injected by a keyboard that disappeared before note-off.
            failed(Error(`MIDI device disconnected: ${event.port.name}`));
        };
        return {devices: [...access.inputs.values(), ...access.outputs.values()].filter(p => p.state === "connected")
            .map(p => ({id: key(p), name: p.name, input: p.type === "input", node: routes.get(key(p)) ?? -1}))};
    }
    async function connect(id, node) {
        await list();
        const port = [...access.inputs.values(), ...access.outputs.values()].find(p => key(p) === id);
        if (!port || port.state !== "connected" || !Number.isInteger(node) || node < -1) throw Error("Invalid MIDI connection");
        // Publish a route only once the device accepted the operation.
        const old = routes.get(id);
        if (node < 0) { await port.close(); routes.delete(id); }
        else { await port.open(); routes.set(id, node); }
        if (port.type === "input") {
            port.onmidimessage = null;
            if (node >= 0) port.onmidimessage = event => {
                if (!timer) return;
                try {
                    const player = getPlayer();
                    if (player?.status === 0) player.midi(node, event.data);
                } catch (error) { failed(error); }
            };
        }
        if (port.type === "output") {
            if (old >= 0 && ![...access.outputs.values()].some(p => routes.get(key(p)) === old)) getPlayer().captureMidi(old, false);
            if (node >= 0) getPlayer().captureMidi(node, true);
        }
        return list();
    }
    async function detach() {
        let failure;
        try { stop(); }
        catch (error) { failure = error; }
        for (const node of new Set([...routes].filter(([id]) => id.startsWith("output:")).map(([, node]) => node))) {
            try { getPlayer()?.captureMidi(node, false); }
            catch (error) { failure ||= error; }
        }
        const connected = new Set(routes.keys());
        routes.clear();
        for (const port of [...access?.inputs.values() || [], ...access?.outputs.values() || []]) {
            port.onmidimessage = null;
            if (!connected.has(key(port))) continue;
            try { await port.close(); }
            catch (error) { failure ||= error; }
        }
        if (failure) throw failure;
    }
    return {list, connect, stop, start, detach};
}
