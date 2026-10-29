// Catalog data is shared by Settings and the examples dropdown.
export function library(request, open, fail) {
    const byId = id => document.getElementById(id);
    const examples = byId("examples"), list = byId("catalog-list"), paths = byId("plugin-paths");
    let data, loading, error, enabled = false, used = new Map();
    const names = {perone: "Perone", clap: "CLAP", lv2: "LV2", vst3: "VST3"};
    function element(tag, text, className) {
        const node = document.createElement(tag);
        node.textContent = text;
        if (className) node.className = className;
        return node;
    }
    function render() {
        if (!data) return;
        const search = byId("catalog-search").value.toLowerCase(), filter = byId("catalog-filter").value;
        const entries = data.plugins.filter(entry =>
            `${entry.name} ${entry.path} ${entry.id || ""} ${entry.format}`.toLowerCase().includes(search) &&
            (filter === "all" || (filter === "unavailable" ? entry.error : entry.format === filter)));
        list.replaceChildren();
        for (const entry of entries) {
            const row = element("div", "", "catalog-item"), heading = element("div", "", "catalog-heading");
            row.dataset.path = entry.path;
            row.dataset.available = String(!entry.error);
            const count = used.get(entry.id ? `${entry.path}\n${entry.id}` : entry.path);
            const status = entry.error ? "Unavailable" : entry.kind;
            heading.append(element("span", entry.name),
                element("small", `${names[entry.format] || entry.format} · ${status}${count ? ` · ${count} in use` : ""}`));
            row.append(heading, element("div", entry.path, "catalog-path"));
            if (entry.id) row.append(element("div", entry.id, "catalog-path"));
            const note = entry.error || (entry["ui-error"] && `GUI unavailable: ${entry["ui-error"]}`);
            if (note) row.append(element("div", note, "catalog-note"));
            list.append(row);
        }
        if (!entries.length) list.append(element("p", data.plugins.length ? "No matches" : "No plugins", "settings-note"));
        const unavailable = data.plugins.filter(entry => entry.error).length;
        byId("catalog-title").textContent = `${data.plugins.length - unavailable} available${unavailable ? ` · ${unavailable} unavailable` : ""}`;
    }
    const lines = input => [...new Set(input.value.split("\n").map(line => line.trim()).filter(Boolean))];
    const inputs = () => [...paths.querySelectorAll("textarea")];
    const changed = () => inputs().some((input, i) => JSON.stringify(lines(input)) !== JSON.stringify(data.sources[i].paths));
    function status() {
        const disabled = !enabled || !data || !!loading, dirty = !!data && changed();
        examples.disabled = byId("plugin-defaults").disabled = disabled;
        byId("plugin-apply").disabled = disabled || !(dirty || data.settingsError);
        byId("plugin-revert").disabled = disabled || !dirty;
        byId("plugin-refresh").disabled = disabled || dirty;
        paths.inert = disabled;
        byId("settings-catalog").setAttribute("aria-busy", String(!!loading));
        byId("catalog-status").textContent = loading ? "Scanning…" : dirty ? "Unsaved" : "";
    }
    function sources() {
        paths.replaceChildren();
        for (const source of data.sources) {
            const row = element("div", "", "plugin-source"), label = element("label", names[source.format] || source.format);
            const body = element("div", "");
            if (data.editable) {
                const input = document.createElement("textarea");
                input.id = `plugin-paths-${source.format}`;
                input.rows = Math.min(4, Math.max(2, source.paths.length + 1));
                input.value = source.paths.join("\n");
                input.spellcheck = false;
                input.wrap = "off";
                input.setAttribute("aria-label", `${names[source.format]} search paths`);
                input.oninput = status;
                label.htmlFor = input.id;
                body.append(input);
                const missing = (source.roots || []).filter(root => root.error);
                if (missing.length) body.append(element("div", missing.map(root => `${root.path}: ${root.error}`).join(" · "), "catalog-note"));
            } else for (const path of source.paths) body.append(element("div", path, "catalog-path"));
            row.append(label, body); paths.append(row);
        }
        byId("plugin-path-actions").hidden = !data.editable;
        byId("plugin-source-title").textContent = data.editable ? "Paths" : "Catalog";
        byId("plugin-path-help").hidden = !data.editable;
        byId("plugin-path-help").textContent = "One path per line; blank disables discovery.";
        byId("settings-location").textContent = data.settingsPath || "";
    }
    async function load(op = "library", settings) {
        if (loading) return loading;
        if (data && op === "library") { fail(error); return; }
        loading = (async () => {
            try {
                error = undefined;
                fail(null);
                const result = await request(op, ...(settings ? [settings] : []));
                const plugins = [...(result.plugins || [])], scores = [];
                for (const file of result.files) {
                    if (file.path.endsWith(".perone/product.json")) {
                        const {product} = JSON.parse(file.text);
                        const effect = product.buses.some(bus => bus.type === "audio" && bus.direction === "input" && !bus.sidechain);
                        plugins.push({format: "perone", path: file.path.slice(0, -"/product.json".length), name: product.name || product.bundleName,
                            id: product.bundleName, kind: effect ? "Effect" : "Instrument"});
                    } else if (file.path.endsWith(".janet") && !file.path.startsWith("examples/sources/")) scores.push(file.path);
                }
                data = {...result, plugins: plugins.sort((a, b) => (a.name || a.path).localeCompare(b.name || b.path) || a.path.localeCompare(b.path))};
                examples.length = 1;
                for (const path of scores.sort()) examples.add(new Option(path.replace(/^examples\//, ""), path));
                sources(); render();
                if (data.settingsError) error = `Cannot read settings: ${data.settingsError}. Apply to save the paths shown.`;
            } catch (cause) { error = cause; }
            finally { loading = undefined; if (error) fail(error); status(); }
        })();
        status();
        return loading;
    }
    byId("catalog-search").oninput = byId("catalog-filter").onchange = render;
    byId("plugin-refresh").onclick = () => load("library-refresh");
    byId("plugin-apply").onclick = () => {
        const selected = {};
        for (const [i, input] of inputs().entries()) {
            const paths = lines(input), source = data.sources[i];
            if (JSON.stringify(paths) !== JSON.stringify(source.defaults)) selected[source.format] = paths;
        }
        load("library-save", JSON.stringify(selected));
    };
    byId("plugin-defaults").onclick = () => {
        for (const [i, input] of inputs().entries()) input.value = data.sources[i].defaults.join("\n");
        status();
    };
    byId("plugin-revert").onclick = () => { sources(); status(); };
    examples.onchange = () => {
        const path = examples.value;
        examples.value = "";
        if (path) open(path);
    };
    return {
        load,
        status(value) { enabled = value; status(); },
        score(score) {
            used = new Map();
            for (const node of score?.nodes || []) if (node.bundle) {
                const key = node.product?.id ? `${node.bundle}\n${node.product.id}` : node.bundle;
                used.set(key, (used.get(key) || 0) + 1);
            }
            render();
        }
    };
}
