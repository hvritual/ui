"""F7-02B PUI codec/source/policy contracts. Synthetic assets are NOT pixel proof."""
from __future__ import annotations
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"scripts"))
import chart_sdk as charts
import pui
import public_sdk as sdk

class ChartSDKTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root=Path(temp.name)
        self.assets=self.root/"assets"
        self.assets.mkdir()
        for filename,content in {
            "application.js":b"unused-app",
            "catalog.json":b"[]",
            "labels.atlas":b"synthetic-atlas-unit-only",
            "builtin.rgba":b"synthetic-image-unit-only"
        }.items():
            (self.assets/filename).write_bytes(content)

    def test_capability_contract(self):
        c=charts.check()
        self.assertEqual(c["runtime_capability"],"ui.draw.chart")
        self.assertEqual(c["point_capacity"],1000)
        self.assertTrue(c["bubbles"])
        self.assertFalse(c["physical_hardware"])
        self.assertFalse(c["signed"])

    def test_real_pui_codec_keeps_chart_opt_in(self):
        packet=charts.package(self.assets)
        for profile in charts.TARGETS:
            v=pui.verify(packet,profile,allow_unsigned=True)
            self.assertEqual(v["capabilities"],513)
            self.assertEqual(v["app_id"],"com.pocket.chart1000")
            self.assertFalse(v["authenticated"])
        with self.assertRaises(pui.PackageError):
            pui.verify(packet,charts.TARGETS[0],allow_unsigned=False)
        with self.assertRaises(pui.PackageError):
            pui.verify(packet,charts.TARGETS[0],capabilities=255,allow_unsigned=True)
        with self.assertRaises(pui.PackageError):
            pui.verify(packet,charts.TARGETS[0],sdk_api=2,allow_unsigned=True)

    def test_tamper_and_unknown_bit_rejected(self):
        original=bytearray(charts.package(self.assets))
        original[-1]^=1
        with self.assertRaises(pui.PackageError):
            pui.verify(bytes(original),charts.TARGETS[0],allow_unsigned=True)
        data=bytearray(charts.package(self.assets))
        data[32:36]=(513|256).to_bytes(4,"little")
        data[160:192]=pui.container_digest(data)
        with self.assertRaises(pui.PackageError):
            pui.verify(bytes(data),charts.TARGETS[0],allow_unsigned=True)

    def test_bounded_generated_source_order(self):
        data=charts.package(self.assets)
        n=int.from_bytes(data[36:40],"little")
        start=192+n*96
        for i in range(n):
            at=192+i*96
            if data[at:at+48].split(b"\0",1)[0]==b"application.js":
                offset=int.from_bytes(data[at+48:at+56],"little")
                length=int.from_bytes(data[at+56:at+64],"little")
                source=data[start+offset:start+offset+length].decode()
                a=source.index('Object.defineProperty(global, "PocketUI"')
                b=source.index('Object.defineProperty(global,"PocketCharts"')
                c=source.index('PocketUI.define(')
                self.assertLess(a,b)
                self.assertLess(b,c)
                self.assertIn('chart.set',source)
                self.assertLess(length,256*1024)
                return
        self.fail("packaged application entry missing")

    def test_reject_bad_manifest_claims(self):
        manifest=json.loads(charts.MANIFEST.read_text())
        for bad in (
            {"capabilities":["ui.core"]},
            {"sdk_api":2},
            {"authentication":"hmac"},
            {"runtime_api":{"min":2,"max":2}},
            {"budgets":{"heap_bytes":65536,"asset_bytes":16777216}}
        ):
            path=self.root/("bad"+str(len(list(self.root.glob("bad*"))))+".json")
            path.write_text(json.dumps({**manifest,**bad}))
            with patch.object(charts,"MANIFEST",path):
                with self.assertRaises((sdk.PublicSDKError,pui.PackageError)):
                    charts.check()

    def test_missing_assets_and_symlinks_rejected(self):
        (self.assets/"chart-helper.bin").write_bytes(b"x")
        with self.assertRaises(sdk.PublicSDKError):
            charts.package(self.assets)
        (self.assets/"chart-helper.bin").unlink()
        (self.assets/"labels.atlas").unlink()
        (self.assets/"labels.atlas").symlink_to(self.root/"missing")
        with self.assertRaises(sdk.PublicSDKError):
            charts.package(self.assets)

    def test_output_overwrite_and_live_elf_required(self):
        dest=self.root/"test.pui"
        digest=charts.pack(self.assets,dest)
        self.assertEqual(digest,sdk.digest(dest))
        original=dest.read_bytes()
        with self.assertRaises(FileExistsError):
            charts.pack(self.assets,dest)
        self.assertEqual(dest.read_bytes(),original)
        with self.assertRaises(sdk.PublicSDKError):
            charts.run(self.assets,self.root/"missing-native",self.root/"missing-arm",self.root/"out")

if __name__=="__main__":
    unittest.main()
