"""F7-02A data/manifest/checker tests. Synthetic assets are not real pixels."""
from __future__ import annotations
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"scripts"))
import custom_sdk as mod
import pui
import public_sdk as base

class CustomSDKTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.assets=self.root/"resources"
        self.assets.mkdir()
        for name,data in {
            "application.js":b"unused-original",
            "catalog.json":b"[]",
            "labels.atlas":b"test-atlas",
            "builtin.rgba":b"test-image"
        }.items():
            (self.assets/name).write_bytes(data)

    def test_contract_declares_no_native_draw(self):
        result=mod.check()
        self.assertEqual(result["draw_protocol"],0)
        self.assertFalse(result["native_renderer_extension"])
        self.assertFalse(result["full_polyline_supported"])
        self.assertFalse(result["physical_hardware"])

    def test_real_R3_container_accepts_custom_as_core_only(self):
        data=mod.package(self.assets)
        for target in mod.TARGETS:
            verified=pui.verify(data,target,allow_unsigned=True)
            self.assertEqual(verified["app_id"],"com.pocket.custom-chart")
            self.assertEqual(verified["capabilities"],1)
            self.assertEqual(verified["sdk_api"],1)
            self.assertFalse(verified["authenticated"])
        with self.assertRaises(pui.PackageError):
            pui.verify(data,mod.TARGETS[0],allow_unsigned=False)
        broken=bytearray(data);broken[-1]^=1
        with self.assertRaises(pui.PackageError):
            pui.verify(bytes(broken),mod.TARGETS[0],allow_unsigned=True)

    def test_encoded_payload_contains_modules_in_exact_order(self):
        data=mod.package(self.assets)
        n=int.from_bytes(data[36:40],"little")
        beginning=192+n*96
        for i in range(n):
            p=192+i*96
            name=data[p:p+48].split(b"\x00",1)[0]
            if name==b"application.js":
                offset=int.from_bytes(data[p+48:p+56],"little")
                length=int.from_bytes(data[p+56:p+64],"little")
                source=data[beginning+offset:beginning+offset+length].decode()
                self.assertLess(source.index('Object.defineProperty(global, "PocketUI"'),
                                source.index('Object.defineProperty(global,"PocketCustom"'))
                self.assertLess(source.index('Object.defineProperty(global,"PocketCustom"'),
                                source.index('PocketCustom.register("pocket.bar-chart"'))
                self.assertLess(source.index('PocketCustom.register("pocket.bar-chart"'),
                                source.index("PocketCustom.mount("))
                self.assertIn("PocketUI.define(",source)
                return
        self.fail("application.js missing")

    def test_unsigned_no_implicit_override(self):
        data=mod.package(self.assets)
        with self.assertRaises(pui.PackageError):
            pui.verify(data,mod.TARGETS[0],sdk_api=2,allow_unsigned=True)
        with self.assertRaises(pui.PackageError):
            pui.verify(data,mod.TARGETS[0],capabilities=0,allow_unsigned=True)

    def test_output_immutable(self):
        dest=self.root/"chart.pui"
        result=mod.package_to(self.assets,dest)
        self.assertEqual(result,base.digest(dest))
        original=dest.read_bytes()
        with self.assertRaises(FileExistsError):
            mod.package_to(self.assets,dest)
        self.assertEqual(dest.read_bytes(),original)

    def test_invalid_resources_fail_before_build(self):
        (self.assets/"helper.so").write_bytes(b"rejected")
        with self.assertRaises(base.PublicSDKError):
            mod.package(self.assets)
        (self.assets/"helper.so").unlink()
        original=self.assets/"labels.atlas"
        original.unlink()
        original.symlink_to(self.root/"unverified")
        with self.assertRaises(base.PublicSDKError):
            mod.package(self.assets)

    def test_bad_contract_budget_rejects(self):
        contract=json.loads(mod.CONTRACT.read_text())
        contract["limits"]["max_primitives_per_instance"]=2000
        bad=self.root/"bad.json";bad.write_text(json.dumps(contract))
        with patch.object(mod,"CONTRACT",bad):
            with self.assertRaises(base.PublicSDKError):
                mod.check()

    def test_widget_cannot_import_native_bridge(self):
        extra=self.root/"foreign.js"
        extra.write_text(mod.WIDGET.read_text()+"\n__pocketCall('engine.internal');\n")
        with patch.object(mod,"WIDGET",extra):
            with self.assertRaises(base.PublicSDKError):
                mod.check()

    def test_dangerous_example_import_denied(self):
        extra=self.root/"import.js"
        extra.write_text(mod.APP.read_text()+"\nrequire('internal');\n")
        with patch.object(mod,"APP",extra):
            with self.assertRaises(base.PublicSDKError):
                mod.check()

    def test_real_renderer_requires_existing_ELFs(self):
        with self.assertRaises(base.PublicSDKError):
            mod.integration(self.assets,self.root/"native",self.root/"arm",self.root/"out")

if __name__=="__main__":
    unittest.main()
