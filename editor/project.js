// A project owns documents and an entry point. Editing and playback keep separate snapshots.
export function resolve(path, base = "/") {
    if (typeof path !== "string" || path.includes("\0")) throw Error("Invalid document path");
    const parts = [];
    for (const part of (path.startsWith("/") ? path : `${base}/${path}`).split("/")) {
        if (part === "..") parts.pop();
        else if (part && part !== ".") parts.push(part);
    }
    return "/" + parts.join("/");
}

export const directory = path => path.slice(0, path.lastIndexOf("/")) || "/";

export function documentText(text) {
    if (typeof text !== "string" || text.includes("\0")) throw Error("Document must be text without NUL bytes");
    return text;
}

export function relative(path, base) {
    const a = base.split("/").filter(Boolean), b = path.split("/").filter(Boolean);
    while (a.length && b.length && a[0] === b[0]) { a.shift(); b.shift(); }
    return [...a.map(() => ".."), ...b].join("/") || ".";
}

export class Project {
    constructor(cwd, root, manifest = "") {
        this.cwd = cwd;
        this.root = root;
        this.manifest = manifest;
        this.documents = new Map();
        this.entry = "";
        this.active = "";
        this.saved = "";
    }

    add(path, text, saved = true) {
        const key = resolve(path, this.cwd);
        if (!key.endsWith(".janet")) throw Error("Documents must have a .janet extension");
        documentText(text);
        if (!this.documents.has(key)) this.documents.set(key, {path, text, saved: saved ? text : undefined});
        this.entry ||= key;
        this.active = key;
        return this.documents.get(key);
    }

    get current() { return this.documents.get(this.active); }

    description() {
        return JSON.stringify({entry: relative(this.entry, this.root), files: [...this.documents.keys()].map(path => relative(path, this.root))}, null, 2) + "\n";
    }

    dirty() {
        return this.description() !== this.saved || [...this.documents.values()].some(doc => doc.text !== doc.saved);
    }

    snapshot() {
        return {project: this, entry: this.entry, path: this.documents.get(this.entry).path,
            sources: Object.fromEntries([...this.documents].map(([path, doc]) => [path, documentText(doc.text)]))};
    }

    matches(snapshot) {
        return snapshot?.project === this && snapshot.entry === this.entry && this.documents.size === Object.keys(snapshot.sources).length &&
            [...this.documents].every(([path, doc]) => snapshot.sources[path] === doc.text);
    }
}

export async function openProject(path, text, cwd, read) {
    const project = new Project(cwd, directory(resolve(path, cwd)), path.endsWith(".tb.json") ? path : "");
    if (!project.manifest) project.add(path, text);
    else {
        const value = JSON.parse(text);
        if (!value || typeof value.entry !== "string" || !Array.isArray(value.files) || !value.files.length ||
            value.files.some(file => typeof file !== "string" || !file.endsWith(".janet"))) throw Error("Invalid project: expected entry and Janet files");
        for (const file of value.files) {
            const path = resolve(file, project.root);
            if (project.documents.has(path)) throw Error("Duplicate project document: " + file);
            project.add(path, await read(path));
        }
        project.entry = resolve(value.entry, project.root);
        if (!project.documents.has(project.entry)) throw Error("Project entry is missing from files");
        project.active = project.entry;
    }
    project.saved = project.description();
    return project;
}
