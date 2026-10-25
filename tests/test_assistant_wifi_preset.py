"""Verify private credential handling with fake networks only."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("wifi_preset", ROOT / "tools/configure_assistant_wifi.py")
preset = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(preset)


class WifiPresetTests(unittest.TestCase):
    def test_private_file_and_invalid_update_preserves_it(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "private" / "wifi.json"
            preset.write_preset({"ssid": "test-only-network", "password": "test-password"}, path)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            original = path.read_bytes()
            with self.assertRaises(ValueError):
                preset.write_preset({"ssid": "test-network", "password": "short"}, path)
            self.assertEqual(path.read_bytes(), original)

    def test_utf8_byte_limits_and_invalid_fields(self):
        for value in (
            {"ssid": "测试" * 6, "password": "test-password"},
            {"ssid": "", "password": "test-password"},
            {"ssid": "bad\0network", "password": "test-password"},
            {"ssid": "test", "password": "x" * 64},
        ):
            with self.subTest(value=value), self.assertRaises(ValueError):
                preset.validate(value)
        self.assertEqual(preset.validate({"ssid": "测试", "password": ""})["password"], "")

    def test_symlink_is_not_followed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target"
            target.write_text("unchanged")
            link = root / "wifi.json"
            link.symlink_to(target)
            with self.assertRaises(ValueError):
                preset.write_preset({"ssid": "test-network", "password": "test-password"}, link)
            self.assertEqual(target.read_text(), "unchanged")


if __name__ == "__main__":
    unittest.main()
