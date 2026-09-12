import {startEditor} from "./main.js";

try { await startEditor(await import("../web/editor.js")); }
catch (error) {
    const output = document.getElementById("errors");
    output.hidden = false;
    output.textContent = String(error);
}
