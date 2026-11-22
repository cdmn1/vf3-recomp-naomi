import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class CartImageFormatTests(unittest.TestCase):
    def test_c_header_parser(self):
        with tempfile.TemporaryDirectory() as td:
            exe = pathlib.Path(td) / "cart-image-format-test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "platform/naomi"),
                str(ROOT / "platform/naomi/cart_image_format.c"),
                str(ROOT / "tools/naomi/test_cart_image_format.c"),
                "-o", str(exe),
            ], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", result.stdout)

    def test_fixed_and_banked_crom_window(self):
        with tempfile.TemporaryDirectory() as td:
            exe = pathlib.Path(td) / "cart-rom-test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "platform/naomi"),
                str(ROOT / "platform/naomi/cart_image_format.c"),
                str(ROOT / "platform/naomi/cart_image.c"),
                str(ROOT / "platform/naomi/cart_rom.c"),
                str(ROOT / "tools/naomi/test_cart_rom.c"),
                "-o", str(exe),
            ], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", result.stdout)

    def test_dma_reader_with_mock_cartridge(self):
        with tempfile.TemporaryDirectory() as td:
            exe = pathlib.Path(td) / "cart-image-runtime-test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "platform/naomi"),
                str(ROOT / "platform/naomi/cart_image_format.c"),
                str(ROOT / "platform/naomi/cart_image.c"),
                str(ROOT / "tools/naomi/test_cart_image_runtime.c"),
                "-o", str(exe),
            ], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", result.stdout)

    def test_vrom_texture_port_streaming(self):
        with tempfile.TemporaryDirectory() as td:
            exe = pathlib.Path(td) / "cart-vrom-test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "platform/naomi"),
                str(ROOT / "platform/naomi/cart_image_format.c"),
                str(ROOT / "platform/naomi/cart_image.c"),
                str(ROOT / "platform/naomi/cart_vrom.c"),
                str(ROOT / "tools/naomi/test_cart_vrom.c"),
                "-o", str(exe),
            ], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", result.stdout)

    def test_model3_rom_bridge(self):
        with tempfile.TemporaryDirectory() as td:
            objects = []
            for name in ("cart_image_format", "cart_image", "cart_rom", "cart_vrom", "vf3_image"):
                source = ROOT / "platform/naomi" / (name + ".c")
                obj = pathlib.Path(td) / (name + ".o")
                subprocess.run([
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "platform/naomi"), "-c", str(source),
                    "-o", str(obj),
                ], check=True)
                objects.append(str(obj))
            exe = pathlib.Path(td) / "cart-rom-bridge-test"
            subprocess.run([
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "platform/naomi"),
                str(ROOT / "platform/naomi/cart_vrom_bridge.cpp"),
                str(ROOT / "platform/naomi/vf3_image_runtime.cpp"),
                str(ROOT / "tools/naomi/test_cart_vrom_bridge.cpp"),
                *objects, "-o", str(exe),
            ], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", result.stdout)


    def test_naomi_pvr_buffer(self):
        with tempfile.TemporaryDirectory() as td:
            include_dir = pathlib.Path(td) / "include"
            pvr_header = include_dir / "dc" / "pvr.h"
            pvr_header.parent.mkdir(parents=True)
            pvr_header.write_text(
                "#pragma once\n#include <stddef.h>\ntypedef void *pvr_ptr_t;\n"
                "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
                "size_t pvr_mem_available(void);\n"
                "pvr_ptr_t pvr_mem_malloc(size_t);\n"
                "void pvr_mem_free(pvr_ptr_t);\n"
                "#ifdef __cplusplus\n}\n#endif\n"
            )
            exe = pathlib.Path(td) / "naomi-pvr-buffer-test"
            subprocess.run([
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(include_dir), "-I", str(ROOT / "platform/naomi"),
                str(ROOT / "tools/naomi/test_naomi_pvr_vector.cpp"),
                "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    unittest.main()
