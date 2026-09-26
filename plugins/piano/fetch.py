"""Fetch the piano samples, using only Python's standard library."""
from pathlib import Path
import hashlib
import io
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2] / ".deps"
FONT_SHA256 = "8d5cab8434f794e66d9b49064422c9e2434b77e8b976358197d3a482dba9bbd6"


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
    name = "000_Florestan_Piano.sf2"
    destination = ROOT / "florestan-piano" / name
    if destination.exists() and hashlib.sha256(destination.read_bytes()).hexdigest() == FONT_SHA256:
        print(destination.relative_to(ROOT.parent), "(cached)")
        raise SystemExit(0)
    archive = fetch("https://dev.nando.audio/_static/sf2/000_Florestan_Piano.zip",
                    "67550e4a21020bcb74a254c5de1c1e13221920a141f0a06d2f55c1797d5f6046")
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        data = z.read(name)
        if hashlib.sha256(data).hexdigest() != FONT_SHA256:
            raise ValueError("SoundFont checksum mismatch")
        install(destination, data)
