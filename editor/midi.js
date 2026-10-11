// Device routing belongs to the environment; the score only describes MIDI streams.
export function midiSettings(request, fail) {
    const panel = document.getElementById("settings-midi");
    const list = document.getElementById("midi-devices");
    let score, devices, loading = false;
    function render() {
        const discover = document.getElementById("midi-enable");
        discover.disabled = loading;
        discover.textContent = devices ? "Refresh" : "Discover";
        discover.title = devices ? "Refresh devices; desktop routes will disconnect" : "";
        list.replaceChildren();
        for (const port of devices || []) {
            const row = document.createElement("label"), name = document.createElement("span");
            row.className = "midi-device";
            name.textContent = `${port.input ? "In" : "Out"} · ${port.name}`;
            const select = document.createElement("select");
            select.setAttribute("aria-label", name.textContent);
            select.add(new Option("Disconnected", "-1"));
            for (const [id, node] of (score?.nodes || []).entries())
                if (node[port.input ? "midiIn" : "midiOut"]) select.add(new Option(`${id + 1} · ${node.label || node.name}`, id));
            select.value = String(port.node);
            select.disabled = !score || loading;
            select.title = score ? "" : "Prepare a score first";
            select.onchange = () => query("midi-connect", score.controlRevision, port.id, Number(select.value));
            row.append(name, select); list.append(row);
        }
        document.getElementById("midi-empty").hidden = !devices || !!devices.length || loading;
    }
    async function query(op, ...args) {
        if (loading) return;
        loading = true;
        render();
        try { ({devices} = await request(op, ...args)); }
        catch (error) { fail(error); }
        finally { loading = false; render(); }
    }
    document.getElementById("midi-enable").onclick = () => query(devices ? "midi-refresh" : "midi-devices");
    return {
        show() { if (devices) query("midi-devices"); },
        score(value) {
            if (score?.controlRevision !== value?.controlRevision) {
                for (const port of devices || []) port.node = -1;
            }
            score = value;
            render();
            if (!panel.hidden && devices) query("midi-devices");
        }
    };
}
