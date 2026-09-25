"""Fetch the optional piano's pinned dependencies, using Python's stdlib."""
from pathlib import Path
import hashlib
import io
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2] / ".deps"
REV = "853a0a171759f1ddba0de1442133a75912bbeffa"


def fetch(url, digest=None):
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()
    if digest and hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"Checksum mismatch: {url}")
    return data


def install(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)
    print(path.relative_to(ROOT.parent))


if __name__ == "__main__":
    for filename, digest in [
        ("tsf.h", "70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197"),
        ("LICENSE", None),
    ]:
        install(ROOT / "tinysoundfont" / filename, fetch(
            f"https://raw.githubusercontent.com/schellingb/TinySoundFont/{REV}/{filename}", digest))
    archive = fetch("https://dev.nando.audio/_static/sf2/000_Florestan_Piano.zip",
                    "67550e4a21020bcb74a254c5de1c1e13221920a141f0a06d2f55c1797d5f6046")
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        name = "000_Florestan_Piano.sf2"
        install(ROOT / "florestan-piano" / name, z.read(name))
