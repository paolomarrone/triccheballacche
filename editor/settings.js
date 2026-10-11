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
    const categories = ["audio", "plugins", "midi"];
    let category = "plugins", enabled = false, switching = false, state = {};

    async function show() {
        byId("settings-error").hidden = true;
        if (category === "plugins") await catalog.load();
        if (category === "midi") midi.show();
        if (category === "audio") {
            try {
                const info = await request("audio-info");
                byId("audio-output").textContent = info.output;
                byId("audio-rate").textContent = `${info.sampleRate.toLocaleString()} Hz`;
                byId("audio-latency").textContent = info.latency ? `${(info.latency * 1000).toFixed(1)} ms` : "—";
            } catch (error) { report(error); }
        }
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
    engine.onchange = async () => {
        switching = true;
        update();
        report(null);
        try { await request("audio", Number(engine.checked)); }
        catch (error) { report(error); }
        switching = false;
        update();
    };
    function update() {
        byId("settings").disabled = !enabled;
        engine.disabled = !enabled || !state.prepared || switching;
        if (!switching) engine.checked = !!state.audio;
        engine.closest("label").title = state.prepared ? "" : "Prepare a score first";
        catalog.status(enabled);
    }
    return {
        load: catalog.load,
        status(value, current) { enabled = value; state = current; update(); },
        score(score) { catalog.score(score); midi.score(score); }
    };
}
