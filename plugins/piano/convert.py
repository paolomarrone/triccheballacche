"""Compile the Florestan SF2 preset into PCM and regions for the piano engine.

This is a build tool, not a general SoundFont player. Unsupported synthesis
features fail explicitly; reverb/chorus sends are ignored (the piano is dry).
Format reference: https://www.synthfont.com/sfspec24.pdf
"""
import argparse
import array
import hashlib
from pathlib import Path
import struct
import sys


DEFAULTS = {8: 13500, 9: 0, 11: 0, 17: 0, 48: 0, 51: 0, 52: 0, 56: 100}
for start in (25, 33):
    DEFAULTS.update({start + i: -12000 for i in (0, 1, 2, 3, 5)})
    DEFAULTS.update({start + i: 0 for i in (4, 6, 7)})
SUPPORTED = set(DEFAULTS) | {0, 1, 2, 3, 4, 12, 15, 16, 41, 43, 44, 45, 50, 53, 54, 58}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def chunks(data):
    result = {}
    position = 0
    while position < len(data):
        require(position + 8 <= len(data), "Truncated RIFF chunk")
        tag, size = struct.unpack_from("<4sI", data, position)
        end = position + 8 + size
        require(end + (size & 1) <= len(data), "RIFF chunk outside file")
        require(tag not in result, f"Duplicate chunk {tag!r}")
        result[tag] = data[position + 8:end]
        position = end + (size & 1)
    return result


def records(data, fmt):
    require(len(data) >= struct.calcsize(fmt) and len(data) % struct.calcsize(fmt) == 0,
            "Invalid SoundFont table size")
    return list(struct.iter_unpack(fmt, data))


def zones(tables, prefix, first, last, terminal):
    bags = records(tables[prefix + b"bag"], "<HH")
    generators = records(tables[prefix + b"gen"], "<HH")
    require(0 <= first < last < len(bags), "Invalid zone range")
    global_zone, result = {}, []
    for index in range(first, last):
        begin, end = bags[index][0], bags[index + 1][0]
        require(0 <= begin <= end <= len(generators), "Invalid generator range")
        zone = {}
        for op, amount in generators[begin:end]:
            require(op in SUPPORTED, f"Unsupported SoundFont generator {op}")
            if prefix == b"p":
                require(op in set(DEFAULTS) | {15, 16, 41, 43, 44}, "Sample generator at preset level")
            else:
                require(op != 41, "Instrument reference inside instrument")
            require(op not in zone, f"Duplicate generator {op}")
            zone[op] = amount if op in (41, 43, 44, 53, 54, 58) else struct.unpack("<h", struct.pack("<H", amount))[0]
        if terminal not in zone:
            require(index == first, "Global zone must come first")
            global_zone = zone
        else:
            result.append(global_zone | zone)
    return result


def intersect(a, b, op):
    ranges = [(z.get(op, 0x7f00) & 255, z.get(op, 0x7f00) >> 8) for z in (a, b)]
    require(all(0 <= lo <= hi <= 127 for lo, hi in ranges), "Invalid key/velocity range")
    return max(r[0] for r in ranges), min(r[1] for r in ranges)


def cfloat(value):
    return float(value).hex() + "f"


def envelope(values, start):
    # Keep timecents until note-on: hold and decay can depend on the MIDI key.
    return "{" + ", ".join(cfloat(values[start + i]) for i in range(8)) + "}"


def read_bank(data):
    require(len(data) >= 12 and data[:4] == b"RIFF" and data[8:12] == b"sfbk", "Expected SF2 RIFF bank")
    require(struct.unpack_from("<I", data, 4)[0] + 8 == len(data), "Invalid RIFF length")
    # The three top-level chunks all have the LIST tag.
    lists, offset = {}, 12
    while offset < len(data):
        require(offset + 12 <= len(data), "Truncated LIST")
        tag, size, kind = struct.unpack_from("<4sI4s", data, offset)
        require(tag == b"LIST" and size >= 4 and offset + 8 + size <= len(data), "Invalid LIST")
        require(kind not in lists, "Duplicate LIST")
        lists[kind] = chunks(data[offset + 12:offset + 8 + size])
        offset += 8 + size + (size & 1)
    require(offset == len(data), "Invalid LIST padding")
    require(all(key in lists for key in (b"INFO", b"sdta", b"pdta")), "Missing SoundFont sections")
    require(lists[b"INFO"].get(b"ifil", b"")[:2] == b"\x02\x00", "Only SF2 is supported")
    require(b"sm24" not in lists[b"sdta"], "24-bit samples are unsupported")
    tables = lists[b"pdta"]
    require(all(key in tables for key in (b"phdr", b"pbag", b"pgen", b"pmod", b"inst", b"ibag", b"igen", b"imod", b"shdr")),
            "Missing SoundFont tables")
    # Ignore only the terminal modulator record. Its unused fields can contain garbage.
    for name in (b"pmod", b"imod"):
        require(len(records(tables[name], "<HHhHH")) == 1, "Custom modulators are unsupported")
    presets = records(tables[b"phdr"], "<20sHHHIII")
    instruments = records(tables[b"inst"], "<20sH")
    samples = records(tables[b"shdr"], "<20sIIIIIBbHH")
    require(len(presets) == 2 and len(instruments) >= 2 and len(samples) >= 2, "Expected one piano preset")
    pcm = lists[b"sdta"].get(b"smpl", b"")
    require(0 < len(pcm) <= 0xffffffff * 2 and len(pcm) % 2 == 0, "Missing or invalid PCM16 data")
    regions = []
    coverage = [[0] * 128 for _ in range(128)]
    for preset in zones(tables, b"p", presets[0][3], presets[1][3], 41):
        instrument = preset[41]
        require(instrument < len(instruments) - 1, "Invalid instrument index")
        for zone in zones(tables, b"i", instruments[instrument][1], instruments[instrument + 1][1], 53):
            key_lo, key_hi = intersect(preset, zone, 43)
            vel_lo, vel_hi = intersect(preset, zone, 44)
            if key_lo > key_hi or vel_lo > vel_hi:
                continue
            sample_id = zone[53]
            require(sample_id < len(samples) - 1, "Invalid sample index")
            _, begin, end, loop_begin, loop_end, rate, root, correction, _, kind = samples[sample_id]
            require(kind in (1, 2, 4), "Only uncompressed mono/stereo PCM is supported")
            values = {op: zone.get(op, default) + preset.get(op, 0) for op, default in DEFAULTS.items()}
            offsets = [zone.get(op, 0) + 32768 * zone.get(coarse, 0)
                       for op, coarse in ((0, 4), (1, 12), (2, 45), (3, 50))]
            begin, end, loop_begin, loop_end = [value + delta for value, delta in
                                              zip((begin, end, loop_begin, loop_end), offsets)]
            loop = zone.get(54, 0)
            require(loop in (0, 1, 3), "Unsupported loop mode")
            require(0 <= begin < end <= len(pcm) // 2 and 0 < rate <= 384000, "Sample outside PCM data")
            require(not loop or begin <= loop_begin < loop_end <= end, "Invalid sample loop")
            root = zone.get(58, root)
            require(0 <= root <= 127, "Invalid root key")
            require(0 <= values[56] <= 1200 and 0 <= values[9] <= 960, "Invalid tuning/filter resonance")
            require(all(-32768 <= values[op] <= 12000 for op in range(25, 41)), "Invalid envelope")
            tuning = values[51] * 100 + values[52] + correction
            require(-12199 <= tuning <= 12199 and -12000 <= values[11] <= 12000, "Invalid pitch/filter modulation")
            require(all(-1200 <= values[op] <= 1200 for op in (31, 32, 39, 40)), "Invalid envelope key scaling")
            # Preserve this instrument's established output level. The bank's
            # preset attenuation is deliberately calibrated at 0.01 dB/unit.
            gain = 10 ** (-max(0, min(1440, values[48])) / 2000)
            fields = [str(x) for x in (begin, end, loop_begin, loop_end, rate, key_lo, key_hi, vel_lo, vel_hi, root, loop)]
            fields += [cfloat(x) for x in (tuning, values[56], gain, max(-.5, min(.5, values[17] / 1000)),
                                          max(1500, min(13500, values[8])), values[9], values[11])]
            fields += [envelope(values, 33), envelope(values, 25)]
            regions.append("\t{" + ", ".join(fields) + "},\n")
            for key in range(key_lo, key_hi + 1):
                for velocity in range(vel_lo, vel_hi + 1):
                    coverage[key][velocity] += 2  # Two unison layers per region.
    require(regions, "No playable regions")
    require(len(regions) <= 4096, "Too many piano regions")
    require(max(map(max, coverage)) <= 192, "A note exceeds the piano's voice capacity")
    return pcm, regions


def convert(source, destination):
    data = source.read_bytes()
    pcm, regions = read_bank(data)
    values = array.array("h", pcm)
    if sys.byteorder != "little":
        values.byteswap()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    try:
        with temporary.open("w", encoding="ascii") as out:
            out.write("// Generated by convert.py; do not edit. Samples: Nando Florestan.\n")
            out.write(f"// Source SHA-256: {hashlib.sha256(data).hexdigest()}\n")
            out.write("static const int16_t piano_samples[] = {\n")
            for i in range(0, len(values), 24):
                out.write("\t" + ",".join(map(str, values[i:i + 24])) + ",\n")
            out.write("};\nstatic const PianoRegion piano_regions[] = {\n")
            out.writelines(regions)
            out.write("};\n")
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"{destination}: {len(values)} PCM samples, {len(regions)} regions")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        convert(args.source, args.destination)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Piano bank: {error}\n")
