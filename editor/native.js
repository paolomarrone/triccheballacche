export const saveLabel = "Save", saveTitle = "Save · Ctrl+S";

export async function connect() {
    // The bridge creates its socket at DOMContentLoaded; readiness is separate from script loading.
    for (let attempt = 0; ; ++attempt) {
        try { await command("status"); return; }
        catch (error) {
            if (attempt === 30) throw error;
            await new Promise(resolve => setTimeout(resolve, 100));
        }
    }
}

export async function command(op, ...args) {
    if (op === "message") args[2] = args[2].map(byte => byte.toString(16).padStart(2, "0")).join("");
    const reply = JSON.parse(await webui.call("command", op, ...args));
    const state = reply.state;
    if (state?.view?.score) state.view.score.controlRevision = state.controlRevision;
    return {...reply, ...state, ...state?.view, error: reply.error || state?.view?.error || ""};
}

export function uiUrl(node, revision, id) {
    return new URL(`/perone/${revision}/${id}/${node.product.ui.web}`, location.href).href;
}
