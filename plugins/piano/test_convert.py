"""Validate bank conversion and rejection without external Python packages."""
from pathlib import Path
import struct
import tempfile
import unittest

from convert import convert, read_bank


def chunk(tag, data):
    return tag + struct.pack("<I", len(data)) + data + b"\0" * (len(data) & 1)


def fixture(generator=38, sample_end=4, loop_end=3, modulator=False):
    info = chunk(b"LIST", b"INFO" + chunk(b"ifil", struct.pack("<HH", 2, 1)))
    pcm = struct.pack("<hhhh", -32768, -1, 0, 32767)
    sound = chunk(b"LIST", b"sdta" + chunk(b"smpl", pcm))
    preset = struct.pack("<20sHHHIII", b"piano", 0, 0, 0, 0, 0, 0)
    preset += struct.pack("<20sHHHIII", b"EOP", 0, 0, 2, 0, 0, 0)
    # Global attenuation and local release; instrument attack inherited globally.
    pgen = struct.pack("<HHHHHH", 48, 240, generator, 1200, 41, 0)
    igen = struct.pack("<HHHHHHHHHHHH", 34, 600, 43, 0x4030, 54, 1, 58, 60, 38, 0, 53, 0)
    tables = {
        b"phdr": preset,
        b"pbag": struct.pack("<HHHHHH", 0, 0, 1, 0, 3, 0),
        b"pmod": b"\0" * (20 if modulator else 10),
        b"pgen": pgen,
        b"inst": struct.pack("<20sH20sH", b"instrument", 0, b"EOI", 2),
        b"ibag": struct.pack("<HHHHHH", 0, 0, 1, 0, 6, 0),
        b"imod": b"\0" * 10,
        b"igen": igen,
        b"shdr": struct.pack("<20sIIIIIBbHH", b"sample", 0, sample_end, 1, loop_end, 32000, 60, -2, 0, 1)
        + b"\0" * 46,
    }
    return chunk(b"RIFF", b"sfbk" + info + sound + chunk(b"LIST", b"pdta" + b"".join(chunk(k, v) for k, v in tables.items())))


class BankTests(unittest.TestCase):
    def test_pcm_and_zone_inheritance(self):
        pcm, regions = read_bank(fixture())
        self.assertEqual(struct.unpack("<hhhh", pcm), (-32768, -1, 0, 32767))
        self.assertEqual(len(regions), 1)
        self.assertIn("0, 4, 1, 3, 32000, 48, 64, 0, 127, 60, 1", regions[0])
        # The volume envelope inherits attack=600 and adds preset release=1200.
        envelope = regions[0].split("{")[2].split("}")[0]
        times = [float.fromhex(x.strip()[:-1]) for x in envelope.split(",")]
        self.assertEqual(times[1], 600)
        self.assertEqual(times[5], 1200)

    def test_truncation(self):
        data = fixture()
        for end in range(len(data)):
            with self.assertRaises(ValueError):
                read_bank(data[:end])

    def test_unsupported_features_and_bounds(self):
        for options in ({"generator": 5}, {"sample_end": 99}, {"loop_end": 99}, {"modulator": True}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                read_bank(fixture(**options))

    def test_reproducible_output_and_failed_conversion(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.sf2"
            output = Path(directory) / "piano_data.h"
            source.write_bytes(fixture())
            convert(source, output)
            first = output.read_bytes()
            self.assertIn(b"-32768,-1,0,32767,", first)
            convert(source, output)
            self.assertEqual(first, output.read_bytes())
            source.write_bytes(fixture(sample_end=999))
            with self.assertRaises(ValueError):
                convert(source, output)
            self.assertEqual(first, output.read_bytes())
            self.assertFalse(output.with_suffix(".h.tmp").exists())


if __name__ == "__main__":
    unittest.main()
