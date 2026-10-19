"""Public SDK manifest/packaging boundary tests. Synthetic files are NOT valid UI pixels."""
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


class PublicSDKTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        # Resource codec fixtures intentionally cannot render, because the
        # real-Framework job separately admits actual licensed/correct assets.
        for name in ("application.js", "catalog.json", "labels.atlas", "builtin.rgba"):
            (self.assets / name).write_bytes({"application.js": b"fake-app",
                                              "catalog.json": b"[]",
                                              "labels.atlas": b"fake-atlas",
                                              "builtin.rgba": b"fake-image"}[name])

    def test_current_versions_match_native_loader(self):
        c = s.check()
        self.assertEqual(c["sdk_version"], "0.1-experimental")
        self.assertEqual(c["sdk_api"], 1)
        self.assertEqual(c["capabilities"], ["ui.core"])
        self.assertFalse(c["production_admission"])
        self.assertFalse(c["physical_hardware"])

    def test_real_r3_codec_pack_and_dual_viewport_parser(self):
        payload = s.packaged(self.assets)
        for target in s.TARGETS:
            report = pui.verify(payload, target, allow_unsigned=True)
            self.assertEqual(report["capabilities"], 1)
            self.assertEqual(report["app_id"], "com.pocket.sdk-example")
            self.assertFalse(report["authenticated"])
            self.assertFalse(report["executed"])
        with self.assertRaises(pui.PackageError):
            pui.verify(payload, s.TARGETS[0], allow_unsigned=False)
        with self.assertRaises(pui.PackageError):
            pui.verify(payload, s.TARGETS[0], allow_unsigned=True, sdk_api=2)
        with self.assertRaises(pui.PackageError):
            pui.verify(payload, s.TARGETS[0], allow_unsigned=True, capabilities=0)
        damaged = bytearray(payload)
        damaged[-1] ^= 1
        with self.assertRaises(pui.PackageError):
            pui.verify(bytes(damaged), s.TARGETS[0], allow_unsigned=True)

    def test_packed_source_is_the_public_facade_and_demo(self):
        data = s.packaged(self.assets)
        count = struct.unpack_from("<I", data, 8 + 7 * 4)[0]
        base = 192 + count * 96
        for i in range(count):
            at = 192 + i * 96
            name = data[at:at + 48].split(b"\x00", 1)[0]
            if name == b"application.js":
                start, length = struct.unpack_from("<QQ", data, at + 48)
                source = data[base + start:base + start + length].decode()
                self.assertIn('Object.defineProperty(global, "PocketUI"', source)
                self.assertIn("PocketUI.define(", source)
                self.assertEqual(source.count("global.__pocketCall"), 2)
                return
        self.fail("public entry absent")

    def test_output_fails_on_overwrite(self):
        dest = self.root / "demo.pui"
        report = s.save_package(self.assets, dest)
        self.assertEqual(report["sha256"], s.digest(dest))
        original = dest.read_bytes()
        with self.assertRaises(FileExistsError):
            s.save_package(self.assets, dest)
        self.assertEqual(dest.read_bytes(), original)

    def test_missing_or_extraneous_assets_reject(self):
        (self.assets / "outside.so").write_bytes(b"\x00")
        with self.assertRaises(s.PublicSDKError):
            s.packaged(self.assets)
        (self.assets / "outside.so").unlink()
        (self.assets / "labels.atlas").unlink()
        with self.assertRaises(s.PublicSDKError):
            s.packaged(self.assets)

    def test_symlinked_or_missing_source_reject(self):
        p = self.assets / "catalog.json"
        p.unlink()
        p.symlink_to(self.root / "untrusted")
        with self.assertRaises(s.PublicSDKError):
            s.packaged(self.assets)

    def test_future_sdk_or_privilege_claim_reject(self):
        manifest = json.loads(s.MANIFEST.read_text())
        cases = [
            {"sdk_api": 2},
            {"capabilities": ["ui.core", "ipc.command"]},
            {"authentication": "ed25519"},
            {"runtime_api": {"min": 2, "max": 2}},
            {"targets": ["imx6ul-1024x600"]},
            {"budgets": {"heap_bytes": 100, "asset_bytes": 16777216}}
        ]
        for changes in cases:
            bad = self.root / ("future-" + str(len(list(self.root.glob("future-*")))) + ".json")
            bad.write_text(json.dumps({**manifest, **changes}))
            with patch.object(s, "MANIFEST", bad):
                with self.assertRaises((s.PublicSDKError, pui.PackageError)):
                    s.check()

    def test_app_importing_native_bridge_rejected(self):
        original = s.APP.read_text()
        malicious = self.root / "malicious.js"
        malicious.write_text(original + "\n__pocketCall('native.shell', 1);\n")
        with patch.object(s, "APP", malicious):
            with self.assertRaisesRegex(s.PublicSDKError, "private API"):
                s.check()

    def test_no_host_renderer_or_ime_source_in_public_api(self):
        sdk = s.FACADE.read_text()
        app = s.APP.read_text()
        for token in ("#include", "/dev/fb0", "lv_obj", "pocket_runtime.c", "socket(", "memfd_create"):
            self.assertNotIn(token, sdk + "\n" + app)

    def test_integration_rejects_missing_production_runtime(self):
        with self.assertRaises(s.PublicSDKError):
            s.integration(self.root / "missing-native", self.root / "missing-arm",
                          self.assets, self.root / "output")

if __name__ == "__main__":
    unittest.main()
