// Catalog data is shared by Settings and the examples dropdown; browsing never creates DSPs.
export function library(request, open, fail) {
    const byId = id => document.getElementById(id);
    const dialog = byId("settings-dialog"), examples = byId("examples"), list = byId("catalog-list");
    let data, loading, enabled = false, used = new Map();

    function element(tag, text, className) {
        const node = document.createElement(tag);
        node.textContent = text;
        if (className) node.className = className;
        return node;
    }

    function render() {
        if (!data) return;
        list.replaceChildren();
        for (const entry of data.plugins) {
            const row = element("div", "", "catalog-item"), heading = element("div", "", "catalog-heading");
            row.dataset.path = entry.path;
            row.dataset.available = String(!entry.error);
            const count = used.get(entry.id ? `${entry.path}\n${entry.id}` : entry.path);
            const status = entry.error ? "Unavailable" : entry.kind;
            heading.append(element("span", entry.name),
                element("small", `${entry.format || "Perone"} · ${status}${count ? ` · ${count} in use` : ""}`));
            row.append(heading, element("div", entry.path, "catalog-path"));
            if (entry.id) row.append(element("div", entry.id, "catalog-path"));
            const note = entry.error || (entry["ui-error"] && `GUI unavailable: ${entry["ui-error"]}`);
            if (note) row.append(element("div", note, "catalog-note"));
            list.append(row);
        }
        const unavailable = data.plugins.filter(entry => entry.error).length;
        byId("catalog-title").textContent = `Plugins · ${data.plugins.length - unavailable} available${unavailable ? ` · ${unavailable} unavailable` : ""}`;
    }

    async function load() {
        if (data) return;
        try {
            loading ||= request("library").then(result => {
                const plugins = [...(result.plugins || [])], scores = [];
                for (const file of result.files) {
                    if (file.path.endsWith(".perone/product.json")) {
                        const {product} = JSON.parse(file.text);
                        const effect = product.buses.some(bus => bus.type === "audio" && bus.direction === "input" && !bus.sidechain);
                        plugins.push({path: file.path.slice(0, -"/product.json".length), name: product.name || product.bundleName,
                            kind: effect ? "Effect" : "Instrument"});
                    } else if (file.path.endsWith(".janet") && !file.path.startsWith("examples/sources/")) {
                        // Supporting modules remain available to imports and the file picker.
                        scores.push(file.path);
                    }
                }
                data = {plugins: plugins.sort((a, b) => a.name.localeCompare(b.name) || a.path.localeCompare(b.path))};
                byId("plugin-paths").replaceChildren(...result.paths.map(path => element("li", path)));
                for (const path of scores.sort()) examples.add(new Option(path.replace(/^examples\//, ""), path));
                render();
            });
            await loading;
        } catch (error) {
            loading = undefined;
            list.textContent = "Cannot read catalog. Reopen Settings to retry.";
            fail(error);
        }
        examples.disabled = !enabled || !data;
    }

    byId("settings").onclick = () => { dialog.showModal(); load(); };
    byId("settings-plugins").onclick = () => byId("settings-catalog").focus();
    examples.onchange = () => {
        const path = examples.value;
        examples.value = "";
        if (path) open(path);
    };

    return {
        load,
        status(value) {
            enabled = value;
            byId("settings").disabled = !value;
            examples.disabled = !value || !data;
        },
        score(score) {
            used = new Map();
            for (const node of score.nodes) if (node.bundle) {
                const key = node.product?.id ? `${node.bundle}\n${node.product.id}` : node.bundle;
                used.set(key, (used.get(key) || 0) + 1);
            }
            render();
        }
    };
}
