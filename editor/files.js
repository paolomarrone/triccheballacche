// Both backends expose directory entries; paths stay meaningful for imports and saving.
export function files(request, open, upload) {
    const byId = id => document.getElementById(id);
    const dialog = byId("file-dialog"), directory = byId("file-directory"), list = byId("file-list");
    const error = byId("file-error"), input = byId("file-input");
    let current = ".", version = 0;

    async function browse(path) {
        const pending = ++version;
        error.hidden = true;
        list.inert = true;
        byId("file-upload").disabled = true;
        try {
            const result = await request("files", path);
            if (!dialog.open || pending !== version) return;
            current = directory.value = result.path;
            byId("file-upload").disabled = false;
            list.replaceChildren();
            const entries = result.files.filter(file => file.directory || file.name.endsWith(".janet"));
            entries.sort((a, b) => Number(b.directory) - Number(a.directory) || a.name.localeCompare(b.name));
            for (const file of entries) {
                const button = document.createElement("button");
                button.textContent = file.name + (file.directory ? "/" : "");
                button.dataset.name = file.name;
                button.onclick = () => {
                    const path = `${current.replace(/\/$/, "")}/${file.name}`;
                    if (file.directory) browse(path);
                    else { dialog.close(); open(path); }
                };
                list.append(button);
            }
            if (!entries.length) {
                const empty = document.createElement("p");
                empty.textContent = "No Janet files or folders.";
                list.append(empty);
            }
        } catch (cause) {
            if (pending !== version) return;
            error.textContent = cause.message;
            error.hidden = false;
        } finally {
            if (pending === version) list.inert = false;
        }
    }

    byId("file-location").onsubmit = event => { event.preventDefault(); browse(directory.value); };
    byId("file-up").onclick = () => browse(current + "/..");
    byId("file-upload").onclick = () => input.click();
    input.onchange = () => {
        const file = input.files[0];
        input.value = "";
        if (file) { dialog.close(); upload(file, current); }
    };
    dialog.onclose = () => { ++version; };

    return (path, local) => {
        byId("file-local").hidden = !local;
        dialog.showModal();
        browse(path.slice(0, path.lastIndexOf("/") + 1) || ".");
    };
}
