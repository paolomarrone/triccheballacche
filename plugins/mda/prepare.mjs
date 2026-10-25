import {cp, mkdir, readdir, readFile, writeFile} from "node:fs/promises";
import {join} from "node:path";
import {execFileSync} from "node:child_process";

const [source, dest] = process.argv.slice(2);
await mkdir(dest, {recursive: true});
for (const name of await readdir(join(source, "mda-vst/src"))) {
    if (!/\.(h|cpp)$/.test(name) || name.includes("GUI")) continue;
    const text = await readFile(join(source, "mda-vst/src", name), "utf8");
    await writeFile(join(dest, name), text.replace(/#include "(?:public.sdk\/source\/vst2.x\/)?audioeffectx.h"/g, '#include "mda.h"'));
}
execFileSync("git", ["apply", "--unsafe-paths", `--directory=${dest}`, "patches/mda-perone.patch"]);
await cp(join(source, "LICENSE"), join(dest, "LICENSE"));
