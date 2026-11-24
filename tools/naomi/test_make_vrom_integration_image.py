import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

MODULE_PATH = Path(__file__).with_name("make_vrom_integration_image.py")
SPEC = importlib.util.spec_from_file_location("make_vrom_integration_image", MODULE_PATH)
IMAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMAGE)


class SyntheticImageTests(unittest.TestCase):
    def test_image_header_regions_crc_and_model_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "probe.bin"
            output = root / "combined.bin"
            executable.write_bytes(b"naomi-test-executable")
            manifest = IMAGE.build(executable, output)

            self.assertEqual(output.stat().st_size, manifest["image_size"])
            with output.open("rb") as stream:
                stream.seek(IMAGE.PAYLOAD_OFFSET)
                header = bytearray(stream.read(IMAGE.HEADER_SIZE))
                self.assertEqual(header[:8], b"VF3NROM\0")
                stored_crc = struct.unpack_from("<I", header, 28)[0]
                struct.pack_into("<I", header, 28, 0)
                self.assertEqual(stored_crc, zlib.crc32(header) & 0xFFFFFFFF)

                descriptors = [
                    struct.unpack_from("<IIII", header, 32 + i * 16)
                    for i in range(3)
                ]
                self.assertEqual([d[0] for d in descriptors], [1, 2, 3])
                self.assertEqual([d[2] for d in descriptors], [
                    IMAGE.FIXED_SIZE, IMAGE.BANKED_SIZE, IMAGE.VROM_SIZE
                ])
                self.assertEqual([d[3] for d in descriptors], [
                    IMAGE.crc_marker_region(IMAGE.FIXED_SIZE, IMAGE.FIXED_BUS_MARKER),
                    IMAGE.crc_marker_region(IMAGE.BANKED_SIZE, IMAGE.BANKED_BUS_MARKER,
                                              IMAGE.BANKED_BUS_MARKER_OFFSET),
                    IMAGE.crc_vrom_region(),
                ])

                fixed_offset, banked_offset = descriptors[0][1], descriptors[1][1]
                stream.seek(fixed_offset)
                self.assertEqual(stream.read(4), struct.pack("<I", IMAGE.FIXED_BUS_MARKER))
                stream.seek(banked_offset + IMAGE.BANKED_BUS_MARKER_OFFSET)
                self.assertEqual(stream.read(4), struct.pack("<I", IMAGE.BANKED_BUS_MARKER))

                vrom_offset = descriptors[2][1]
                stream.seek(vrom_offset + IMAGE.MODEL_BYTE_OFFSET)
                self.assertEqual(stream.read(len(IMAGE.MODEL_BYTES)), IMAGE.MODEL_BYTES)
                stream.seek(vrom_offset + IMAGE.VROM_EDGE_WORD_ADDRESS * 4)
                self.assertEqual(stream.read(len(IMAGE.VROM_EDGE_BYTES)), IMAGE.VROM_EDGE_BYTES)

    def test_model_contains_two_independent_terminated_polygons(self):
        self.assertEqual(len(IMAGE.MODEL_WORDS), 38)
        # First polygon is non-terminal; second polygon carries the final marker.
        self.assertEqual(IMAGE.MODEL_WORDS[1] & 4, 0)
        self.assertEqual(IMAGE.MODEL_WORDS[20] & 4, 4)
        self.assertNotEqual(IMAGE.MODEL_WORDS[6], 0)
        self.assertEqual(IMAGE.MODEL_WORDS[25], IMAGE.MODEL_WORDS[6])
        self.assertEqual(IMAGE.MODEL_WORDS[4], 0xFFFFFFFF)
        self.assertEqual(IMAGE.MODEL_WORDS[23], 0xFF0000FF)
        self.assertNotEqual(IMAGE.MODEL_WORDS[4], IMAGE.MODEL_WORDS[23])
        # Both polygons enable smooth per-vertex normals and textured UV input.
        self.assertTrue(IMAGE.MODEL_WORDS[1] & 8)
        self.assertTrue(IMAGE.MODEL_WORDS[20] & 8)
        self.assertTrue(IMAGE.MODEL_WORDS[6] & 0x400)
        self.assertTrue(IMAGE.MODEL_WORDS[25] & 0x400)
        self.assertEqual(IMAGE.MODEL_WORDS[14], 0x01000000)
        self.assertEqual(IMAGE.MODEL_WORDS[18], 0x00000100)
        self.assertEqual(IMAGE.MODEL_WORDS[33], 0x02000100)
        self.assertEqual(IMAGE.MODEL_WORDS[37], 0x01000200)


if __name__ == "__main__":
    unittest.main()
