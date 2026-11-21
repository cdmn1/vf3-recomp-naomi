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


if __name__ == "__main__":
    unittest.main()
