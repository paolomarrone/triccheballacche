"""Prepare the Android CLAP plugins and original sample bank for Due corpi.

Run from any directory. Downloads and generated assets stay in .deps/ and build/.
The isolated working directory keeps Samplo's instrument indices reproducible.
"""
import argparse
import hashlib
import math
import os
from pathlib import Path
import struct
import subprocess
import urllib.request
import wave

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".deps/clap-android"
WORK = ROOT / "build/examples/due-corpi"
PLUGINS = [
    ("samplo-clap", "v1.2.7", "Samplo-android-arm64.clap",
     "299a7f30213ac1e9034648386b4f3be9e939de130be7bc3c0345707b84b50f75"),
    ("vitsel-clap", "v0.7.4", "Vitsel-android-arm64.clap",
     "2dfe3df69a3a8173d466ff376555bd362eb41964aff710166ddaba10cd88d74a"),
]


def download(repo, version, name, digest):
    path = CACHE / name
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == digest:
        return
    url = f"https://github.com/mlm-games/{repo}/releases/download/{version}/{name}"
    print(f"Downloading {name} ({version})", flush=True)
    request = urllib.request.Request(url, headers={"User-Agent": "triccheballacche"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"Checksum mismatch: {name}")
    temporary = path.with_suffix(".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def sample(path, bright, variation):
    """A struck metal tine at C4; all partials and envelopes are original."""
    rate, duration = 48000, 1.4
    frequency = 261.625565 * (1 + variation * 0.0008)
    frames = bytearray()
    for i in range(round(rate * duration)):
        t = i / rate
        attack = min(1, t / 0.0015)
        ending = min(1, (duration - t) / 0.04)
        fundamental = math.sin(math.tau * frequency * t) * math.exp(-t * 4.1)
        overtone = math.sin(math.tau * frequency * 2.756 * t) * math.exp(-t * 8.5)
        strike = math.sin(math.tau * frequency * 6.21 * t) * math.exp(-t * 38)
        value = attack * ending * (0.50 * fundamental + bright * (0.24 * overtone + 0.13 * strike))
        frames.extend(struct.pack("<h", round(32767 * value)))
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        output.writeframes(frames)


def prepare():
    CACHE.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    for item in PLUGINS:
        download(*item)
    # The Janet library, Perone bundles and editor assets use paths from the CWD.
    for name in ["lib", "plugins", "build", "editor"]:
        link = WORK / name
        if not link.exists():
            link.symlink_to(ROOT / name, target_is_directory=True)
    bank = WORK / "instruments"
    bank.mkdir(exist_ok=True)
    regions = []
    for layer, bright, low, high in [("soft", 0.45, 1, 79), ("hard", 1.0, 80, 127)]:
        for variation in range(2):
            name = f"tine-{layer}-{variation}.wav"
            sample(bank / name, bright, variation)
            regions.append(f"<region> sample={name} pitch_keycenter=60 lokey=0 hikey=127 "
                           f"lovel={low} hivel={high} seq_length=2 seq_position={variation + 1}")
    # Samplo 1.2.7 starts with a sine placeholder at index 0 and only loads a
    # sample bank when the index changes. Select the real bank at index 1.
    (bank / "00-warmup.sfz").write_text(
        "<region> sample=tine-soft-0.wav pitch_keycenter=60 lokey=0 hikey=127\n")
    (bank / "01-due-corpi.sfz").write_text("\n".join(regions) + "\n")
    print(f"Prepared {WORK.relative_to(ROOT)}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--render", action="store_true", help="write renders/due-corpi.wav")
    mode.add_argument("--play", action="store_true", help="play with the native CLI")
    mode.add_argument("--gui", action="store_true", help="open the native editor")
    mode.add_argument("--serve", action="store_true", help="serve the native editor in a browser")
    args = parser.parse_args()
    prepare()
    env = os.environ.copy()
    env["CLAP_PATH"] = str(CACHE)
    score = str(ROOT / "examples/due-corpi.janet")
    if args.render:
        destination = ROOT / "renders/due-corpi.wav"
        destination.parent.mkdir(exist_ok=True)
        command = [str(ROOT / "build/cli"), score, str(destination), "48000"]
    elif args.play:
        command = [str(ROOT / "build/cli"), "--play", score, "48000"]
    elif args.gui:
        command = [str(ROOT / "build/gui"), score]
    elif args.serve:
        command = [str(ROOT / "build/gui"), "--serve", score]
    else:
        print("Next: python tools/due-corpi.py --render (or --play / --gui / --serve)")
        return
    subprocess.run(command, cwd=WORK, env=env, check=True)


if __name__ == "__main__":
    main()
