"""Custom SDK contract/codec/oracle tests. Synthetic pixels are not runtime evidence."""
from __future__ import annotations
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import public_sdk as s
import pui


class CustomSDKTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_custom_contract_and_hashed_dependencies(self):
        report = s.check("custom-chart")
        self.assertEqual(report["scope"], "F7-02A-bounded-custom-surface")
        for file in (s.CUSTOM, s.CUSTOM_TYPES, s.CHART, s.CUSTOM_APP, s.CUSTOM_MANIFEST):
            self.assertEqual(report["source"][str(file.relative_to(ROOT))], s.digest(file))
        self.assertEqual(report["capabilities"], ["ui.core"])
        self.assertFalse(report["production_admission"])
        self.assertFalse(report["authenticated"])

    def test_private_api_in_custom_sdk_module_or_app_rejected(self):
        for field in ("CUSTOM", "CHART", "CUSTOM_APP"):
            original = getattr(s, field)
            for expression in ("__pocketCall('ipc.command', 1);", "require('hosts/linux');",
                               "lv_obj_create();", "eval('code');", "new Function('code');"):
                bad = self.root / "bad.js"
                bad.write_text(original.read_text(encoding="utf-8") + "\n" + expression, encoding="utf-8")
                with self.subTest(field=field, expression=expression), patch.object(s, field, bad):
                    with self.assertRaisesRegex(s.PublicSDKError, "private API"):
                        s.check("custom-chart")

    def test_future_surface_version_or_budget_rejected(self):
        for key, value in (("max_primitives", 33), ("version", "1"), ("trust", "sandbox")):
            contract = s.read_json(s.CONTRACT)
            contract["custom_surface"][key] = value
            bad = self.root / "contract.json"
            bad.write_text(json.dumps(contract), encoding="utf-8")
            with patch.object(s, "CONTRACT", bad):
                with self.assertRaisesRegex(s.PublicSDKError, "custom surface contract"):
                    s.check("custom-chart")

    def test_manifest_and_source_budget_fail_closed(self):
        manifest = s.read_json(s.CUSTOM_MANIFEST)
        bad = self.root / "manifest.json"
        for changes in ({"capabilities":["ui.core", "ipc.command"]}, {"sdk_api":2}):
            bad.write_text(json.dumps({**manifest, **changes}), encoding="utf-8")
            with patch.object(s, "CUSTOM_MANIFEST", bad):
                with self.assertRaises(s.PublicSDKError):
                    s.check("custom-chart")
        huge = self.root / "huge.js"
        huge.write_text(s.CHART.read_text(encoding="utf-8") + " " * pui.SOURCE_LIMIT, encoding="utf-8")
        with patch.object(s, "CHART", huge):
            with self.assertRaisesRegex(s.PublicSDKError, "source budget"):
                s.check("custom-chart")

    def test_custom_codec_and_concatenation_order(self):
        assets = self.root / "assets"
        assets.mkdir()
        for name in pui.REQUIRED:
            (assets / name).write_bytes(b"synthetic-codec-fixture")
        payload = s.packaged(assets, "custom-chart")
        for target in s.TARGETS:
            report = pui.verify(payload, target, allow_unsigned=True)
            self.assertEqual(report["app_id"], "com.pocket.custom-chart")
            self.assertEqual(report["capabilities"], 1)
            self.assertFalse(report["executed"])
            with self.assertRaises(pui.PackageError):
                pui.verify(payload, target)
        count = struct.unpack_from("<I", payload, 36)[0]
        base = 192 + count * 96
        entry = next(192 + i * 96 for i in range(count)
                     if payload[192 + i * 96:192 + i * 96 + 48].split(b"\0", 1)[0] == b"application.js")
        offset, length = struct.unpack_from("<QQ", payload, entry + 48)
        source = payload[base + offset:base + offset + length].decode("utf-8")
        self.assertEqual(source, "\n".join(p.read_text(encoding="utf-8") for p in
                         (s.FACADE, s.CUSTOM, s.CHART, s.CUSTOM_APP)) + "\n")
        self.assertEqual(source.count("global.__pocketCall"), 2)

    def test_missing_production_elf_or_baseline_fails_closed(self):
        with self.assertRaisesRegex(s.PublicSDKError, "production ELF"):
            s.integration(self.root / "native", self.root / "arm", self.root, self.root / "out", "custom-chart")
        for mode in ("native", "arm"):
            (self.root / mode).write_bytes(b"not-an-executable")
        stale = self.root / "stale.json"
        stale.write_text(json.dumps({"example":"counter", "source_commit":"stale"}))
        # These isolate admission ordering; no mock executable counts as runtime evidence.
        with patch.object(s, "git", side_effect=lambda *args: "" if args[0] == "status" else "commit"), \
             patch.object(s, "assemble", return_value=set()):
            with self.assertRaisesRegex(s.PublicSDKError, "baseline evidence"):
                s.integration(self.root / "native", self.root / "arm", self.root, self.root / "out", "custom-chart")
            with self.assertRaisesRegex(s.PublicSDKError, "exact counter production binaries"):
                s.integration(self.root / "native", self.root / "arm", self.root, self.root / "out", "custom-chart", stale)
        self.assertFalse((self.root / "out").exists())

    def test_pixel_checker_rejects_blank_malformed_or_wrong_profile(self):
        for data in (b"", b"P6\n1024 800\n255\n", b"P6\n1024 600\n255\n",
                     b"P6\n1024 600\n255\n" + bytes.fromhex("faf8f4") * (1024 * 600)):
            (self.root / "first.ppm").write_bytes(data)
            with self.assertRaises(s.PublicSDKError):
                s.chart_pixels(self.root, 600)


if __name__ == "__main__":
    unittest.main()
