"""Generate Due corpi's original SFZ bank in a Samplo instruments directory."""
import argparse
import math
from pathlib import Path
import struct
import wave


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


def prepare(bank):
    bank.mkdir(parents=True, exist_ok=True)
    regions = []
    for layer, bright, low, high in [("soft", 0.45, 1, 79), ("hard", 1.0, 80, 127)]:
        for variation in range(2):
            name = f"tine-{layer}-{variation}.wav"
            sample(bank / name, bright, variation)
            regions.append(f"<region> sample={name} pitch_keycenter=60 lokey=0 hikey=127 "
                           f"lovel={low} hivel={high} seq_length=2 seq_position={variation + 1}")
    path = bank / "due-corpi.sfz"
    path.write_text("\n".join(regions) + "\n")
    print(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="output directory for the SFZ and samples")
    args = parser.parse_args()
    prepare(args.directory)


if __name__ == "__main__":
    main()
