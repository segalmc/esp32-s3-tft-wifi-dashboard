#!/usr/bin/env python3
"""Check the generated RGB565 image layout without requiring TFT hardware."""

import re
import unittest
from pathlib import Path

from PIL import Image

from build_background import HEIGHT, WIDTH, compose, crop_photo


ROOT = Path(__file__).resolve().parents[1]


class BackgroundAssetTest(unittest.TestCase):
    def test_pixels_checksum_and_row_stride(self):
        with Image.open(ROOT / "assets/source/mountain_sunset_over_lake.jpg") as photo:
            crop = crop_photo(photo)
        expected = compose(crop)
        self.assertEqual(expected.size, (320, 480))
        generated = (ROOT / "src/BackgroundAsset.cpp").read_text(encoding="ascii")
        pixel_text = generated.split("kBackgroundPixels[320 * 480] PROGMEM = {", 1)[1]
        pixel_text = pixel_text.split("};", 1)[0]
        words = [int(value, 16) for value in re.findall(r"0x([0-9A-F]{4})", pixel_text)]
        self.assertEqual(len(words), WIDTH * HEIGHT)
        for x, y in ((0, 0), (319, 0), (0, 1), (319, 479), (24, 220), (178, 421)):
            r, g, b = expected.getpixel((x, y))
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            self.assertEqual(words[y * WIDTH + x], rgb565)
        checksum = sum(words) & 0xFFFFFFFF
        self.assertIn(f"kBackgroundChecksum = 0x{checksum:08X}", generated)
        photo_text = generated.split("kPhotoOnlyPixels[320 * 480] PROGMEM = {", 1)[1]
        photo_text = photo_text.split("};", 1)[0]
        photo_words = [int(value, 16) for value in re.findall(r"0x([0-9A-F]{4})", photo_text)]
        self.assertEqual(len(photo_words), WIDTH * HEIGHT)
        for x, y in ((0, 0), (319, 0), (0, 1), (319, 479), (178, 421)):
            r, g, b = crop.getpixel((x, y))
            self.assertEqual(photo_words[y * WIDTH + x],
                             ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        # The diagnostic photo differs from the dashboard image only where
        # translucent UI surfaces were precomposed.
        self.assertEqual(photo_words[110 * WIDTH + 160], words[110 * WIDTH + 160])
        self.assertNotEqual(photo_words[220 * WIDTH + 40], words[220 * WIDTH + 40])
        self.assertIn(f"kPhotoOnlyChecksum = 0x{sum(photo_words) & 0xFFFFFFFF:08X}", generated)


if __name__ == "__main__":
    unittest.main()
