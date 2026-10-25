"""Verify the generated font's actual maps, not just a requested conversion range."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FontCoverage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.coverage = set()
        for name in ("passport_noto_sans_sc_16.c", "passport_kaomoji_16.c"):
            cls.coverage.update(cls.font_coverage(ROOT / "assets/fonts" / name))

    @staticmethod
    def font_coverage(path):
        source = path.read_text(encoding='utf-8')
        arrays = {
            name: [int(value, 0) for value in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
            for name, body in re.findall(r"static const uint(?:8|16)_t (\w+)\[\] = \{(.*?)\};", source, re.S)
        }
        coverage = set()
        for entry in re.findall(r"\{\s*\.range_start\s*=.*?\}", source, re.S):
            start = int(re.search(r"range_start\s*=\s*(\d+)", entry)[1])
            length = int(re.search(r"range_length\s*=\s*(\d+)", entry)[1])
            offsets = re.search(r"glyph_id_ofs_list\s*=\s*(\w+)", entry)[1]
            sparse = re.search(r"unicode_list\s*=\s*(\w+)", entry)[1]
            if sparse != "NULL":
                coverage.update(start + offset for offset in arrays[sparse])
            elif offsets != "NULL":
                coverage.update(start + i for i, offset in enumerate(arrays[offsets]) if i == 0 or offset)
            else:
                coverage.update(range(start, start + length))
        return coverage

    def test_kaomoji_inventory(self):
        samples = (ROOT / "assets/fonts/kaomoji-samples.txt").read_text(encoding="utf-8")
        self.assertFalse({ord(c) for c in samples if not c.isspace()} - self.coverage)

    def test_common_ideographs(self):
        self.assertTrue(set(range(0x4E00, 0x9FF0)) <= self.coverage)

    def test_fixed_device_text(self):
        for path in (ROOT / "main/main.c", ROOT / "main/assistant_ui.c"):
            # All CJK characters in source include labels and fixed status text.
            required = {ord(c) for c in path.read_text(encoding='utf-8') if "\u4e00" <= c <= "\u9fff"}
            self.assertFalse(required - self.coverage, path.name)
        self.assertTrue({ord(c) for c in "中文，。？！·?"} <= self.coverage)

    def test_unknown_characters_are_not_claimed(self):
        self.assertNotIn(0x1F642, self.coverage)
        self.assertNotIn(0x9FF0, self.coverage)

    def test_large_font_configuration_and_binding(self):
        self.assertIn("CONFIG_LV_FONT_FMT_TXT_LARGE=y", (ROOT / "sdkconfig.defaults").read_text(encoding='utf-8'))
        ui = (ROOT / "main/assistant_ui.c").read_text(encoding='utf-8')
        self.assertIn("lv_obj_set_style_text_font(obj, &s_text_font", ui)
        self.assertIn("s_text_font = passport_noto_sans_sc_16", ui)
        self.assertIn("s_text_font.fallback = &passport_kaomoji_16", ui)
        self.assertIn("glyph.is_placeholder", ui)


if __name__ == "__main__":
    unittest.main()
