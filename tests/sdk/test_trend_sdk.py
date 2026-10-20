"""Source/admission checker tests; no fixture here is a runtime acceptance image."""
from __future__ import annotations
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"scripts"))
import public_sdk as sdk


class TrendSDKTests(unittest.TestCase):
    def test_example_sources_and_limits_match(self):
        result=sdk.check("trend-chart")
        self.assertEqual(result["scope"],"F7-02B-bounded-polyline")
        for path in (sdk.TREND,sdk.TREND_APP,sdk.TREND_MANIFEST):
            self.assertEqual(result["source"][str(path.relative_to(ROOT))],sdk.digest(path))
        self.assertEqual(result["capabilities"],["ui.core"])
        self.assertFalse(result["production_admission"])

    def test_version_or_budget_drift_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"contract.json"
            for key,value in (("version",2),("max_points",8192),("max_surfaces",5),("stroke_width",10)):
                contract=sdk.read_json(sdk.CONTRACT)
                contract["drawing_protocol"][key]=value
                path.write_text(json.dumps(contract),encoding="utf-8")
                with patch.object(sdk,"CONTRACT",path):
                    with self.assertRaisesRegex(sdk.PublicSDKError,"drawing protocol"):
                        sdk.check("trend-chart")

    def test_developer_module_cannot_depend_on_internal_bridge(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"charts.js"
            path.write_text(sdk.TREND.read_text(encoding="utf-8")+"\n__pocketCall('draw.commit',1);",encoding="utf-8")
            with patch.object(sdk,"TREND",path):
                with self.assertRaisesRegex(sdk.PublicSDKError,"private API"):
                    sdk.check("trend-chart")

    def test_blank_pixels_and_wrong_target_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for height in (600,800):
                (root/"first.ppm").write_bytes(f"P6\n1024 {height}\n255\n".encode()+b"\0"*(1024*height*3))
                with self.assertRaisesRegex(sdk.PublicSDKError,"vertex"):
                    sdk.trend_pixels(root,height)
                with self.assertRaisesRegex(sdk.PublicSDKError,"dimensions"):
                    sdk.trend_pixels(root,800 if height==600 else 600)


if __name__=="__main__":
    unittest.main()
