import assert from "node:assert/strict";
import {once} from "node:events";
import {janetTokens} from "../editor/janet.js";
import {serve} from "./server.mjs";
import {withBrowser} from "./chromium.mjs";

const tokens = source => [...janetTokens(source)].map(([from, to, kind]) => [source.slice(from, to), kind]);
assert.deepEqual(tokens('# def :key\n"# string" :key -1.5e2 16rFF 0xff nil'), [
    ["# def :key", "comment"], ['"# string"', "string"], [":key", "keyword"],
    ["-1.5e2", "number"], ["16rFF", "number"], ["0xff", "number"], ["nil", "literal"]
]);
assert.deepEqual(tokens('@``a ` # :x\n音`` @"a\\\"b" "unfinished'), [
    ['@``a ` # :x\n音``', "string"], ['@"a\\\"b"', "string"], ['"unfinished', "string"]
]);
assert.deepEqual(tokens("'@(% 7 3) |(+ $ 1)"), [
    ["'", "punctuation"], ["@(", "punctuation"], ["7", "number"], ["3", "number"], [")", "punctuation"],
    ["|", "punctuation"], ["(", "punctuation"], ["1", "number"], [")", "punctuation"]
]);
assert.deepEqual(tokens('value% 音 1a'), []);

const server = serve(0);
await once(server, "listening");
try {
    await withBrowser(async ({call, evaluate, wait, key, selectText, diagnostics}) => {
        await call("Page.navigate", {url: `http://127.0.0.1:${server.address().port}/test/code.html`});
        await wait("globalThis.editor");
        const text = () => evaluate('document.querySelector("#code").textContent');
        const load = source => evaluate(`editor.load(${JSON.stringify(source)})`);
        const type = async value => {
            for (const c of value) {
                await call("Input.dispatchKeyEvent", {type: "keyDown", key: c, text: c});
                await call("Input.dispatchKeyEvent", {type: "keyUp", key: c});
            }
        };
        const paste = value => evaluate(`(() => {
            const clipboardData = new DataTransfer(); clipboardData.setData("text/plain", ${JSON.stringify(value)});
            document.querySelector("#code").dispatchEvent(new ClipboardEvent("paste", {clipboardData, bubbles: true, cancelable: true}));
        })()`);
        const source = '# <img src=x onerror=alert(1)>\n(def 音 @"hello")\n:cutoff 1200\n';
        await load(source);
        assert.equal(await text(), source);
        assert.equal(await evaluate('document.querySelectorAll("#code img").length'), 0);
        assert.equal(await evaluate('document.querySelector("#code .syntax-keyword").textContent'), ":cutoff");
        assert.equal(await evaluate('document.querySelector("#code .syntax-form").textContent'), "def");

        await load("");
        await selectText("#code", 0);
        await type("(");
        assert.equal(await text(), "()");
        await key("Enter");
        assert.equal(await text(), "(\n  \n)");
        await type("+ 1 2");
        await wait('document.querySelector("#code .syntax-number")?.textContent === "1 2"');
        const before = await text();
        await key("Enter", "Enter", 2);
        assert.equal(await evaluate("commands"), 1);
        assert.equal(await text(), before, "Application shortcuts must not insert a newline");

        await load("abc");
        await selectText("#code", 3);
        await key("Enter");
        await type("x");
        assert.equal(await text(), "abc\nx");

        await load("abc");
        await selectText("#code", 3);
        await type("x");
        await evaluate("new Promise(resolve => setTimeout(resolve, 350))");
        await key("z", "KeyZ", 2);
        assert.equal(await text(), "abc");
        await key("z", "KeyZ", 10);
        assert.equal(await text(), "abcx");
        await selectText("#code", 1, 3);
        await paste("音\n<&>");
        assert.equal(await text(), "a音\n<&>x");
        assert.equal(await evaluate("editor.value"), await text());

        await evaluate("editor.readOnly(true)");
        const locked = await text();
        await type("(");
        await paste("overwrite");
        assert.equal(await text(), locked);
        await evaluate("editor.readOnly(false)");
        await load("new document");
        await selectText("#code", 3);
        await key("z", "KeyZ", 2);
        assert.equal(await text(), "new document", "Undo cannot reach the previous file");

        await load("(def x 10)\n# unchanged\n(def y :foo)\n");
        await evaluate('globalThis.comment = document.querySelector("#code .syntax-comment")');
        await selectText("#code", 7, 9);
        await type("true");
        await wait('document.querySelector("#code .syntax-literal")?.textContent === "true"');
        assert.equal(await text(), "(def x true)\n# unchanged\n(def y :foo)\n");
        assert(await evaluate('document.querySelector("#code .syntax-comment") === comment'), "Editing must preserve unchanged tokens");

        await load(Array.from({length: 200}, (_, i) => `(def value-${i} ${i})\n`).join(""));
        await evaluate(`(() => {
            globalThis.label = document.querySelector("#numbers span");
            globalThis.token = document.querySelector("#code .syntax-form");
            document.querySelector("#code").scrollTop = 200;
        })()`);
        await wait('Number(document.querySelector("#numbers span").dataset.line) > 1');
        assert(await evaluate('document.querySelector("#numbers span") === label'), "Scrolling must reuse gutter rows");
        assert(await evaluate('document.querySelector("#code .syntax-form") === token'), "Scrolling must preserve highlighted text");
        assert(await evaluate('document.querySelector("#numbers").childElementCount < 20'), "Keep gutter work bounded by the viewport");

        assert.deepEqual(diagnostics, []);
        await call("Page.navigate", {url: "about:blank"});
        console.log("OK: Janet tokens, text preservation, highlighting, typing, indentation, shortcuts, undo/redo, paste, read-only and document history");
    });
} finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
}
