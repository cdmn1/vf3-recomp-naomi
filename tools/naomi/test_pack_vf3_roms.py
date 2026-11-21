import importlib.util
import json
from pathlib import Path
import struct
import unittest
import zlib

MODULE_PATH = Path(__file__).with_name("pack_vf3_roms.py")
SPEC = importlib.util.spec_from_file_location("pack_vf3_roms", MODULE_PATH)
PACKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKER)


class PackerTests(unittest.TestCase):
    def test_profile_has_all_39_revision_d_roms(self):
        profile = json.loads(PACKER.PROFILE_PATH.read_text(encoding="utf-8"))
        self.assertEqual(profile["profile"], "vf3-rev-d")
        self.assertEqual(len(profile["roms"]), 39)

    def test_interleaved_byte_lanes_and_pair_swap(self):
        region = bytearray(32)
        rom = {"name": "test", "region": "banked", "offset": 2,
               "stride": 8, "chunk": 2, "swap": True}
        PACKER.place_rom(region, rom, bytes([0x10, 0x11, 0x20, 0x21]))
        self.assertEqual(region[2:4], bytes([0x11, 0x10]))
        self.assertEqual(region[10:12], bytes([0x21, 0x20]))

    def test_host_word_order_matches_romset_cpp(self):
        source = bytearray.fromhex("12345678 90abcdef")
        self.assertEqual(PACKER.to_sh4_word_order(source),
                         bytearray.fromhex("78563412 efcdab90"))

    def test_header_contains_absolute_region_offsets_and_crc(self):
        regions = [{"id": 1, "offset": 0x01001000,
                    "size": 0x800000, "crc32": 0x12345678}]
        header = PACKER.make_header(1, 0x801000, regions)
        magic, version, size, profile, count, payload, stored_crc = struct.unpack_from(
            "<8sIIIIII", header, 0)
        self.assertEqual(magic, PACKER.MAGIC)
        self.assertEqual((version, size, profile, count, payload),
                         (1, 4096, 1, 1, 0x801000))
        check = bytearray(header)
        struct.pack_into("<I", check, 28, 0)
        self.assertEqual(stored_crc, zlib.crc32(check) & 0xFFFFFFFF)
        self.assertEqual(struct.unpack_from("<IIII", header, 32),
                         (1, 0x01001000, 0x800000, 0x12345678))


if __name__ == "__main__":
    unittest.main()
