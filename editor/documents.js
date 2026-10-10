import {Project, openProject, resolve, relative, documentText} from "./project.js";

// Keep one textarea per document so switching tabs preserves native undo and selection.
export function documents(request, action, changed) {
    const byId = id => document.getElementById(id);
    const template = byId("code"), tabs = byId("document-tabs"), entry = byId("project-entry");
    const dialog = byId("document-dialog"), name = byId("document-name");
    const editors = new Map();
    let project, cwd = "/", tabState = "", creatingProject = false;

    function select(key) {
        project.active = key;
        for (const [path, editor] of editors) {
            editor.hidden = path !== key;
            editor.removeAttribute("id");
        }
        if (!editors.has(key)) {
            const doc = project.current, editor = template.cloneNode(false);
            editor.value = doc.text;
            editor.hidden = false;
            editor.setAttribute("aria-label", `Janet document: ${relative(key, project.root)}`);
            for (const event of ["input", "click", "keyup", "select", "scroll"])
                editor.addEventListener(event, () => { doc.text = editor.value; changed(); });
            byId("input").append(editor);
            editors.set(key, editor);
        }
        editors.get(key).id = "code";
        byId("path").value = project.current.path;
        tabState = "";
    }

    function replace(next) {
        for (const editor of editors.values()) editor.remove();
        template.remove();
        editors.clear();
        project = next;
        select(project.active);
    }

    function create(isProject) {
        creatingProject = isProject;
        byId("project-menu").hidePopover();
        byId("document-title").textContent = isProject ? "New project" : "New document";
        name.value = isProject ? "projects/untitled" : "lib/notes.janet";
        byId("document-label").textContent = isProject ? "Project folder" : "Path relative to project folder";
        byId("document-error").hidden = true;
        dialog.showModal();
        name.focus(); name.select();
    }
    byId("new-document").onclick = () => create(false);
    byId("new-project").onclick = () => create(true);
    byId("save-project").onclick = () => { byId("project-menu").hidePopover(); action("project-save"); };
    byId("remove-document").onclick = () => { byId("project-menu").hidePopover(); action("document-remove"); };
    entry.onchange = () => { project.entry = entry.value; changed(); };
    byId("document-form").onsubmit = event => {
        event.preventDefault();
        action(creatingProject ? "project-new" : "document-new", name.value.trim());
    };

    return {
        get project() { return project; },
        get code() { return editors.get(project?.active) || template; },
        dirty: () => project?.dirty(),
        async connect() {
            cwd = (await request("files", ".")).path;
        },
        async start(result) {
            replace(await openProject(result.path, result.text, cwd, async path => (await request("open", path)).text));
        },
        status(enabled) {
            for (const id of ["new-document", "project", "project-entry", "open-document", "save-project", "new-project"])
                byId(id).disabled = !enabled;
            for (const id of ["new-document", "project-entry", "open-document", "save-project"])
                byId(id).disabled ||= !project;
            tabs.inert = !enabled;
            byId("remove-document").disabled = !enabled || !project || project.active === project.entry;
            if (!project) return;
            byId("project-root").textContent = project.root;
            const state = JSON.stringify([project.entry, project.active, [...project.documents].map(([key, doc]) => [key, doc.text !== doc.saved])]);
            if (state === tabState) return;
            tabState = state;
            tabs.replaceChildren();
            entry.replaceChildren();
            for (const [key, doc] of project.documents) {
                const label = relative(key, project.root);
                const button = document.createElement("button");
                button.type = "button";
                button.setAttribute("role", "tab");
                button.setAttribute("aria-selected", key === project.active);
                button.title = `${doc.path}${key === project.entry ? " · Entry point" : ""}`;
                button.textContent = `${key === project.entry ? "▶ " : ""}${label}${doc.text !== doc.saved ? " ●" : ""}`;
                button.dataset.path = key;
                button.onclick = () => { select(key); changed(); };
                tabs.append(button);
                entry.add(new Option(label, key));
            }
            entry.value = project.entry;
        },
        async command(op, path, text) {
            if (!project && !["open", "import", "project-new"].includes(op)) throw Error("Open or create a project first");
            if (op === "open") {
                const result = await request("open", path);
                replace(await openProject(result.path, result.text, cwd, async path => (await request("open", path)).text));
            } else if (op === "project-new") {
                if (!path) throw Error("Enter a project folder");
                const root = resolve(path, cwd), next = new Project(cwd, root, `${root}/project.tb.json`);
                const listing = await request("files", root).catch(() => null);
                if (listing?.files.length) throw Error("Choose a new or empty project folder");
                next.add(`${root}/main.janet`, "", false);
                replace(next);
                dialog.close();
            } else if (op === "document-new") {
                if (!path) throw Error("Enter a document path");
                const key = resolve(path, project.root);
                if (project.documents.has(key)) throw Error("Document is already in this project");
                // A new document never silently replaces an existing file.
                const folder = key.slice(0, key.lastIndexOf("/")), filename = key.split("/").at(-1);
                const listing = await request("files", folder).catch(() => null);
                if (listing?.files.some(file => file.name === filename)) throw Error("File exists; use Open document");
                project.add(key, "", false);
                select(key);
                dialog.close();
            } else if (op === "import") {
                const next = await openProject(path, text, cwd, async path => (await request("open", path)).text);
                for (const doc of next.documents.values()) doc.saved = undefined;
                replace(next);
            } else if (op === "document-open" || op === "document-import") {
                if (op === "document-open" && !project.documents.has(resolve(path, cwd))) text = (await request("open", path)).text;
                if (!project.documents.has(resolve(path, cwd))) project.add(path, text, op === "document-open");
                select(resolve(path, cwd));
            } else if (op === "document-remove") {
                if (project.active === project.entry) throw Error("Choose another entry before removing this document");
                if (project.current.text !== project.current.saved && !confirm("Remove this document and discard its unsaved changes?")) return;
                editors.get(project.active)?.remove();
                editors.delete(project.active);
                project.documents.delete(project.active);
                select(project.entry);
            } else if (op === "save") {
                const doc = project.current;
                if (resolve(path, cwd) !== project.active) throw Error("Open or create a document before saving to another path");
                await request("save", doc.path, documentText(doc.text));
                doc.saved = doc.text;
            } else if (op === "project-save") {
                for (const doc of project.documents.values()) {
                    await request("save", doc.path, documentText(doc.text), "quiet");
                    doc.saved = doc.text;
                }
                const manifest = project.manifest || `${project.root}/project.tb.json`;
                const description = project.description();
                await request("save", manifest, description, "quiet");
                project.manifest = manifest;
                project.saved = description;
            }
        }
    };
}
