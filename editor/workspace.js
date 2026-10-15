// Rearrange the existing views without replacing their contents or session.
export function workspace(closePlugins) {
    const get = id => document.getElementById(id);
    const compact = matchMedia("(max-width: 760px), (pointer: coarse) and (max-height: 500px)");
    const panel = get("plugins"), viewport = window.visualViewport;
    let active = "code", visible = false;

    function view(name) {
        active = name;
        if (compact.matches && name !== "code" && get("code").contains(document.activeElement)) document.activeElement.blur();
        document.body.dataset.view = name;
        for (const value of ["code", "score"]) get(`view-${value}`).setAttribute("aria-pressed", value === name);
    }

    function plugins(value) {
        visible = value;
        get("views").checked = value;
        if (panel.open && (!value || panel.matches(":modal") !== compact.matches)) panel.close();
        panel.hidden = !value;
        if (value && !panel.open) {
            if (compact.matches) panel.showModal();
            else panel.open = true;
        }
    }

    function resize() {
        // The on-screen keyboard resizes the visual viewport, not always the page.
        const fit = compact.matches && viewport?.scale === 1;
        document.body.style.setProperty("--app-height", fit ? `${viewport.height}px` : "100dvh");
        document.body.style.setProperty("--app-top", fit ? `${viewport.offsetTop}px` : "0px");
    }

    for (const name of ["code", "score"]) get(`view-${name}`).onclick = () => view(name);
    get("plugins-close").onclick = closePlugins;
    panel.addEventListener("cancel", event => { event.preventDefault(); closePlugins(); });
    panel.addEventListener("click", event => {
        if (event.target !== panel || !panel.matches(":modal")) return;
        const r = panel.getBoundingClientRect();
        if (event.clientX < r.left || event.clientX > r.right || event.clientY < r.top || event.clientY > r.bottom) closePlugins();
    });
    compact.addEventListener("change", () => { view(active); plugins(visible); resize(); });
    viewport?.addEventListener("resize", resize);
    viewport?.addEventListener("scroll", resize);
    view(active); resize();
    return {view, plugins};
}
