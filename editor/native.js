export const views = true, saveLabel = "Salva", saveTitle = "Salva · Ctrl+S";

export async function connect() {
    // The bridge creates its socket at DOMContentLoaded; readiness is separate from script loading.
    for (let attempt = 0; ; ++attempt) {
        try { await command("status", "", "", false); return; }
        catch (error) {
            if (attempt === 30) throw error;
            await new Promise(resolve => setTimeout(resolve, 100));
        }
    }
}

export async function command(op, ...args) {
    const reply = JSON.parse(await webui.call("command", op, ...args));
    return {...reply, ...reply.view, error: reply.error || reply.view?.error || ""};
}
