"""The physical verifier must reject simulated, stale and incomplete board evidence."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"scripts"))
import public_sdk as sdk
import trend_verify


class BoardVerifierTests(unittest.TestCase):
    def test_rejects_wrong_commit_simulation_bad_count_and_missing_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);bundle=root/"bundle";output=root/"board"
            bundle.mkdir();output.mkdir()
            for name in ("ui-framework","trend-chart.pui"):
                (bundle/name).write_bytes(b"verifier-only fixture, not a usable artifact")
            manifest={"source_commit":"a"*40,"static_arm_sha256":sdk.digest(bundle/"ui-framework"),
                      "package_sha256":sdk.digest(bundle/"trend-chart.pui")}
            (bundle/"manifest.json").write_text(json.dumps(manifest))
            (bundle/"SHA256SUMS").write_text("")
            (output/"display.json").write_text("{}")
            original={"commit":manifest["source_commit"],"profile":sdk.TARGETS[0],"ok":True,"error":None,
                      "physical_io":True,"business_commands":False,"package_authenticated":False,
                      "package_sha256":manifest["package_sha256"],"drawing_points":1024,"drawing_surfaces":1,
                      "drawing_commits":2,"drawing_uploads":2,"drawing_uploaded_points":2048,
                      "item_count":1024,"nodes":8,"presents":2,"bytes_written":4096,"core_live_bytes_after_close":0}
            for changes in ({"physical_io":False},{"commit":"b"*40},{"drawing_points":999},
                            {"package_sha256":"0"*64},{"ok":False},{"core_live_bytes_after_close":1}):
                (output/"report.json").write_text(json.dumps({**original,**changes}))
                with self.assertRaisesRegex(sdk.PublicSDKError,"physical 1024-point"):
                    trend_verify.verify(bundle,output)
            (output/"report.json").write_text(json.dumps(original))
            with self.assertRaises(FileNotFoundError):
                trend_verify.verify(bundle,output)
            (bundle/"ui-framework").write_bytes(b"tampered")
            with self.assertRaisesRegex(sdk.PublicSDKError,"cloud bundle hashes"):
                trend_verify.verify(bundle,output)


if __name__=="__main__":
    unittest.main()
