import importlib.util
import json
from pathlib import Path
import struct
import unittest
import zlib

# The real-image packer must overwrite the runtime payload slot, not append a second image.
# Marker commit: exercise real-ROM packaging in CI after validating the in-place overlay.
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

    def test_pack_replaces_existing_payload_at_runtime_offset(self):
        import tempfile
        from unittest import mock

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "synthetic.bin"
            rom_zip = root / "roms.zip"
            output = root / "real-vf3.bin"
            payload_offset = 0x1000
            test_regions = {
                "fixed_crom": (1, 8),
                "banked_crom": (2, 16),
                "vrom": (3, 8),
            }
            data = {
                "fixed_crom": bytearray(b"FIXED123"),
                "banked_crom": bytearray(b"BANKED-REGION!!!"),
                "vrom": bytearray(b"VROMDATA"),
            }
            offsets = {}
            current = payload_offset + PACKER.HEADER_SIZE
            descriptors = []
            for name, (region_id, _) in test_regions.items():
                offsets[name] = current
                region = data[name]
                descriptors.append({
                    "id": region_id, "offset": current, "size": len(region),
                    "crc32": zlib.crc32(region) & 0xFFFFFFFF,
                })
                current = PACKER.align_up(current + len(region))
            original_header = PACKER.make_header(1, current - payload_offset, descriptors)
            with base.open("wb") as stream:
                stream.write(bytes([0xA5]) * payload_offset)
                stream.write(original_header)
                stream.write(bytes(current - payload_offset - PACKER.HEADER_SIZE))
            rom_zip.write_bytes(b"mocked ROM archive")
            replacement = {
                "fixed_crom": bytearray(b"REALFIXD"),
                "banked_crom": bytearray(b"REAL-BANKED-DATA"),
                "vrom": bytearray(b"REALVROM"),
            }
            with mock.patch.object(PACKER, "REGIONS", test_regions):
                with mock.patch.object(PACKER, "load_verified_roms", return_value=replacement):
                    manifest = PACKER.pack(base, rom_zip, output, payload_offset)

            self.assertEqual(manifest["payload_offset"], payload_offset)
            self.assertEqual(manifest["image_size"], current)
            self.assertEqual(output.stat().st_size, current)
            with output.open("rb") as stream:
                stream.seek(payload_offset)
                self.assertEqual(stream.read(PACKER.HEADER_SIZE)[:8], PACKER.MAGIC)
                for name, region in replacement.items():
                    stream.seek(offsets[name])
                    self.assertEqual(stream.read(len(region)), bytes(region))
            with base.open("rb") as stream:
                stream.seek(payload_offset)
                self.assertEqual(stream.read(PACKER.HEADER_SIZE), original_header)

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
