export const nativeViews = true, views = true, saveLabel = "Salva", saveTitle = "Salva · Ctrl+S";

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
    if (op === "message") args[2] = args[2].map(byte => byte.toString(16).padStart(2, "0")).join("");
    const reply = JSON.parse(await webui.call("command", op, ...args));
    return {...reply, ...reply.view, error: reply.error || reply.view?.error || ""};
}

export function uiUrl(node, revision, id) {
    return new URL(`/perone/${revision}/${id}/${node.product.ui.web}`, location.href).href;
}
