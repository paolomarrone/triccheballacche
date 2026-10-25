import {library} from "./library.js";
import {midiSettings} from "./midi.js";

export function settings(request, open, fail) {
    const byId = id => document.getElementById(id), dialog = byId("settings-dialog"), engine = byId("settings-audio-engine");
    const report = error => {
        byId("settings-error").hidden = !error;
        if (!error) return;
        byId("settings-error").textContent = String(error.message || error);
        if (!dialog.open) fail(error);
    };
    const catalog = library(request, open, report), midi = midiSettings(request, report);
    const rate = byId("audio-rate");
    const categories = ["audio", "plugins", "midi"];
    let category = "plugins", enabled = false, audioPending = false, sampleRate = 0, state = {};

    async function audio(op, value) {
        if (audioPending) return;
        audioPending = true;
        update();
        report(null);
        try { if (op) await request(op, value); }
        catch (error) { report(error); }
        try {
            const info = await request("audio-info");
            sampleRate = info.sampleRate;
            const rates = [...new Set([44100, 48000, 88200, 96000, 176400, 192000, sampleRate])].sort((a, b) => a - b);
            rate.replaceChildren(...rates.map(value => new Option(`${value.toLocaleString()} Hz`, value)));
            rate.value = sampleRate;
            byId("audio-output").textContent = info.output;
            byId("audio-latency").textContent = info.latency ? `${(info.latency * 1000).toFixed(1)} ms` : "—";
        } catch (error) { report(error); }
        audioPending = false;
        update();
    }

    async function show() {
        byId("settings-error").hidden = true;
        if (category === "plugins") await catalog.load();
        if (category === "midi") midi.show();
        if (category === "audio") await audio();
    }
    byId("settings").onclick = () => { dialog.showModal(); show(); };
    for (const name of categories) byId(`settings-${name}-tab`).onclick = () => {
        category = name;
        for (const key of categories) {
            const panel = byId(key === "plugins" ? "settings-catalog" : `settings-${key}`);
            panel.hidden = key !== category;
            byId(`settings-${key}-tab`).setAttribute("aria-current", key === category ? "page" : "false");
            if (!panel.hidden) panel.focus();
        }
        show();
    };
    engine.onchange = () => audio("audio", Number(engine.checked));
    rate.onchange = () => audio("sample-rate", Number(rate.value));
    function update() {
        byId("settings").disabled = !enabled;
        engine.disabled = !enabled || !state.prepared || audioPending;
        if (!audioPending) engine.checked = !!state.audio;
        engine.closest("label").title = state.prepared ? "" : "Apply code or press Play first";
        rate.disabled = !enabled || !sampleRate || state.audio || audioPending;
        rate.parentElement.title = state.audio ? "Turn the engine off to change sample rate" : "Applies on the next Play or Apply";
        catalog.status(enabled);
    }
    return {
        load: catalog.load,
        status(value, current) {
            if (state.prepared && !current.prepared) { catalog.score(null); midi.score(null); }
            enabled = value; state = current; update();
        },
        score(score) { catalog.score(score); midi.score(score); }
    };
}
