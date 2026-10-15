// Lexical highlighting only; evaluation and diagnostics belong to Janet.
const forms = new Set(`def var set defn defmacro fn if when unless cond case do let
    loop each for while repeat break continue return import use quote unquote
    quasiquote splice upscope try catch defer`.split(/\s+/));
const number = /^[+-]?(?:(?:\d[\d_]*(?:\.[\d_]*)?|\.[\d_]+)(?:[eE][+-]?[\d_]+)?|0[xX][\da-fA-F_]+(?:\.[\da-fA-F_]*)?(?:[pP][+-]?[\d_]+)?|\d{1,2}r[\da-zA-Z_]+(?:\.[\da-zA-Z_]*)?(?:&[+-]?[\da-zA-Z_]+)?)$/;

export function* janetTokens(source) {
    const symbol = /[!$%&*+\-./0-9:<=>?@A-Z^_a-z\u0080-\uffff]+/y;
    for (let i = 0; i < source.length;) {
        const start = i;
        let type;
        if (source[i] === "#") {
            const end = source.indexOf("\n", i);
            i = end < 0 ? source.length : end;
            type = "comment";
        } else if (source[i] === '"' || source[i] === "`" ||
            source[i] === "@" && (source[i + 1] === '"' || source[i + 1] === "`")) {
            if (source[i] === "@") ++i;
            if (source[i] === '"') {
                ++i;
                while (i < source.length) {
                    const c = source[i++];
                    if (c === '"') break;
                    if (c === "\\" && i < source.length) ++i;
                }
            } else {
                const begin = i++;
                while (source[i] === "`") ++i;
                const delimiter = source.slice(begin, i), end = source.indexOf(delimiter, i);
                i = end < 0 ? source.length : end + delimiter.length;
            }
            type = "string";
        } else if ("()[]{}',;~|".includes(source[i]) || source[i] === "@" && "([{".includes(source[i + 1])) {
            i += source[i] === "@" ? 2 : 1;
            type = "punctuation";
        } else {
            symbol.lastIndex = i;
            const match = symbol.exec(source);
            if (!match) { ++i; continue; }
            const word = match[0];
            i += word.length;
            type = word.startsWith(":") ? "keyword" : number.test(word) ? "number" :
                ["nil", "true", "false"].includes(word) ? "literal" : forms.has(word) ? "form" : undefined;
        }
        if (type) yield [start, i, type];
    }
}
