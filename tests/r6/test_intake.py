"""Synthetic verifier tests only; no fixture is real device approval."""
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path
import json
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import r6_hil as gate
import r6_intake as intake


class IntakeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.returned, self.manifest = gate.fixture(self.root)
        self.profile = gate.PROFILES[0]

    def run_path(self, scenario="input"):
        return self.returned / self.profile / scenario

    def result(self):
        return intake.inspect(self.returned, self.manifest)

    def change_report(self, **values):
        path = self.run_path() / "runtime/report.json"
        data = gate.load(path); data.update(values); gate.write_json(path, data)
        gate.refresh_sums(self.run_path())

    def input_row(self, result):
        return next(r for r in result["runs"] if r["profile"] == self.profile and r["scenario"] == "input")

    def test_full_fixture_is_consistent_never_board_admitted(self):
        r = self.result()
        self.assertEqual(r["state"], "consistent")
        self.assertEqual(r["exit_code"], 0)
        self.assertFalse(r["board_accepted"])
        self.assertFalse(r["product_admitted"])
        self.assertEqual(r["final_gate"]["state"], "consistent")

    def test_empty_returns_expose_all_four_gaps(self):
        shutil.rmtree(self.returned)
        r = self.result()
        self.assertEqual(len(r["runs"]), 4)
        self.assertEqual(r["exit_code"], 2)
        self.assertTrue(all(x["state"] == "pending" for x in r["runs"]))
        self.assertFalse(self.returned.exists())

    def test_one_input_does_not_hide_other_targets(self):
        shutil.rmtree(self.returned / gate.PROFILES[1])
        shutil.rmtree(self.run_path("coffee"))
        (self.returned / self.profile / "review.json").unlink()
        r = self.result()
        self.assertEqual(self.input_row(r)["state"], "consistent")
        self.assertEqual(r["exit_code"], 2)
        self.assertEqual(sum(x["state"] == "pending" for x in r["runs"]), 3)

    def test_process_ok_is_not_ime_success(self):
        self.change_report(ime_commits=0, ime_candidate_batches=0, ime_provider_stage=5, ime_provider_errno=1)
        row = self.input_row(self.result())
        self.assertEqual(row["state"], "rejected")
        self.assertEqual(row["counters"]["ime_provider_stage"], 5)
        self.assertTrue(any(c["check"] == "scenario-completion" and c["state"] == "rejected" for c in row["checks"]))

    def test_hash_tamper_is_not_pending(self):
        (self.run_path() / "startup.log").write_text("tampered")
        self.assertEqual(self.result()["exit_code"], 1)

    def test_stale_commit_rejected_after_rehash(self):
        self.change_report(commit="f" * 40)
        r = self.input_row(self.result())
        self.assertTrue(any(c["check"] == "runtime" and c["state"] == "rejected" for c in r["checks"]))

    def test_replay_rejected_after_rehash(self):
        self.change_report(synthetic_input=True, replay_samples=100)
        r = self.input_row(self.result())
        self.assertTrue(any(c["check"] == "runtime" and c["state"] == "rejected" for c in r["checks"]))

    def test_missing_safe_frame_is_visible(self):
        (self.run_path() / "runtime/last.ppm").unlink()
        gate.refresh_sums(self.run_path())
        r = self.input_row(self.result())
        self.assertTrue(any("runtime/last.ppm" in c.get("missing", []) for c in r["checks"]))

    def test_multiple_missing_files_reported_together(self):
        for name in ("runtime/last.ppm", "runtime/display.json", "runtime/input.json"):
            (self.run_path() / name).unlink()
        gate.refresh_sums(self.run_path())
        r = self.input_row(self.result())
        required = next(c for c in r["checks"] if c["check"] == "required-original-files")
        self.assertEqual(len(required["missing"]), 3)

    def test_named_review_required(self):
        (self.returned / self.profile / "review.json").unlink()
        r = self.result()
        self.assertEqual(r["exit_code"], 2)
        self.assertEqual(r["reviews"][0]["state"], "pending")

    def test_unapproved_review_never_promoted(self):
        path = self.returned / self.profile / "review.json"
        d = gate.load(path); d["approved"] = False; gate.write_json(path, d)
        self.assertEqual(self.result()["exit_code"], 1)
        self.assertFalse(gate.load(path)["approved"])

    def test_unchanged_final_verifier_detects_controller_mismatch(self):
        run = self.run_path(); p = run / "runtime/input.json"
        d = gate.load(p); d["name"] = "another-controller"; d["expected"]["name"] = "another-controller"; gate.write_json(p, d)
        gate.refresh_sums(run)
        review_path = self.returned / self.profile / "review.json"
        review = gate.load(review_path)
        review["evidence"]["input/runtime/input.json"] = gate.sha(p)
        gate.write_json(review_path, review)
        r = self.result()
        self.assertEqual(r["final_gate"]["state"], "rejected")
        self.assertIn("controller", r["final_gate"]["reason"])

    def test_symlink_report_rejected(self):
        p = self.run_path() / "runtime/report.json"
        external = self.root / "elsewhere.json"; p.rename(external); p.symlink_to(external)
        self.assertEqual(self.result()["exit_code"], 1)

    def test_symlink_root_rejected(self):
        alias = self.root / "alias"; alias.symlink_to(self.returned, target_is_directory=True)
        with self.assertRaises(gate.EvidenceError):
            intake.inspect(alias, self.manifest)

    def test_invalid_json_does_not_crash_or_pass(self):
        (self.run_path() / "runtime/report.json").write_text('{"schema":1,"schema":1}')
        gate.refresh_sums(self.run_path())
        self.assertEqual(self.result()["exit_code"], 1)

    def test_plaintext_report_fields_not_copied(self):
        self.change_report(user_text="PRIVATE_SYNTHETIC_VALUE", candidate_text="PRIVATE_CANDIDATE")
        encoded = json.dumps(self.result())
        self.assertNotIn("PRIVATE_SYNTHETIC_VALUE", encoded)
        self.assertNotIn("PRIVATE_CANDIDATE", encoded)
        self.assertNotIn("user_text", encoded)

    def test_read_only_all_evidence_hashes_unchanged(self):
        before = {str(p): gate.sha(p) for p in self.root.rglob("*") if p.is_file()}
        self.result()
        after = {str(p): gate.sha(p) for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_cli_pending_returns_two_and_external_json(self):
        shutil.rmtree(self.returned / gate.PROFILES[1])
        external = tempfile.TemporaryDirectory()
        self.addCleanup(external.cleanup)
        output = Path(external.name) / "intake.json"
        self.assertEqual(intake.main(["--report", str(self.returned), "--manifest", str(self.manifest), "--output", str(output)]), 2)
        self.assertFalse(json.loads(output.read_text())["board_accepted"])

    def test_cli_never_overwrites_evidence_or_existing_output(self):
        with redirect_stderr(StringIO()):
            self.assertEqual(intake.main(["--report", str(self.returned), "--manifest", str(self.manifest), "--output", str(self.run_path() / "intake.json")]), 1)
            external = tempfile.TemporaryDirectory()
            self.addCleanup(external.cleanup)
            out = Path(external.name) / "result.json"; out.write_text("untouched")
            self.assertEqual(intake.main(["--report", str(self.returned), "--manifest", str(self.manifest), "--output", str(out)]), 1)
            self.assertEqual(out.read_text(), "untouched")

    def test_bundle_corruption_rejected_before_any_acceptance(self):
        (self.manifest.parent / "ui-framework").write_bytes(b"wrong-binary")
        with self.assertRaises(gate.EvidenceError):
            self.result()


if __name__ == "__main__":
    unittest.main()
