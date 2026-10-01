#!/usr/bin/env python3
"""R6 physical-board evidence gate.

This checker validates consistency and completeness of returned evidence for BOTH
physical targets and BOTH operator scenarios (Coffee + optional IME package).
It never creates physical authenticity, product, performance, publisher or
production approval. Human review remains mandatory and automatic runtime
visual_validated must stay false.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path

PROFILES = ("imx6ul-1024x600", "imx6ul-1024x800")
SCENARIOS = ("coffee", "input")
VIDEO_SUFFIXES = (".mp4", ".mov", ".m4v")
HUMAN_CHECKS = (
    "lcd_touch",
    "orientation_edges",
    "coffee_flow",
    "modal_no_clickthrough",
    "drag_cancel_focus",
    "virtualization",
    "media_update_rollback",
    "fault_recovery_no_ghost",
    "text_input_editing",
    "pinyin_candidate_commit",
    "input_locale_switch",
    "sensitive_field_privacy",
    "resource_missing_corrupt",
    "restart_after_fault",
)
IMPORTANT_FILES = (
    "startup.log",
    "runtime/report.json",
    "runtime/display.json",
    "runtime/input.json",
    "runtime/timeline.csv",
    "runtime/first.ppm",
    "runtime/last.ppm",
)


class EvidenceError(ValueError):
    pass


def need(value, message):
    if not value:
        raise EvidenceError(message)


def unique(pairs):
    result = {}
    for key, value in pairs:
        need(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def load(path: Path):
    need(path.is_file() and not path.is_symlink(), "missing/non-regular JSON: " + str(path))
    return json.loads(path.read_text(), object_pairs_hook=unique)


def sha(path: Path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def member(root: Path, relative: str):
    need(type(relative) is str and relative and "\\" not in relative, "invalid evidence path")
    p = Path(relative)
    need(not p.is_absolute() and ".." not in p.parts and "." not in p.parts, "unsafe evidence path: " + relative)
    target = root / p
    need(not target.is_symlink(), "symlink evidence rejected: " + relative)
    return target


def digest(value):
    return type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def verify_run_sums(run: Path):
    sums = run / "SHA256SUMS"
    need(sums.is_file() and not sums.is_symlink(), "missing run SHA256SUMS")
    entries = {}
    for raw in sums.read_text().splitlines():
        need(len(raw) >= 67 and raw[64:66] == "  ", "malformed SHA256SUMS")
        expected, name = raw[:64], raw[66:]
        need(digest(expected), "malformed SHA256 digest")
        if name.startswith("./"):
            name = name[2:]
        target = member(run, name)
        need(target.is_file(), "SHA256SUMS member missing: " + name)
        need(name not in entries, "duplicate SHA256SUMS path: " + name)
        need(sha(target) == expected, "run evidence digest mismatch: " + name)
        entries[name] = expected
    for name in IMPORTANT_FILES:
        need(name in entries, "run manifest missing required file: " + name)
    return entries


def common_runtime_gate(report, profile, manifest):
    need(report.get("schema") == 1, "wrong runtime schema")
    need(report.get("profile") == profile, "wrong runtime profile")
    need(report.get("commit") == manifest["source_commit"], "runtime source commit mismatch")
    need(report.get("ok") is True and report.get("error") is None, "runtime failed")
    need(report.get("physical_io") is True, "physical I/O not evidenced")
    need(report.get("synthetic_input") is not True and report.get("synthetic") is not True, "synthetic run cannot pass R6")
    need(report.get("visual_validated") is False, "automatic report must not claim visual validation")
    need(report.get("business_commands") is False, "business/device commands must remain disabled")
    need(report.get("package_authenticated") in (False, None), "publisher authentication overclaimed")
    for key in ("unblank_errno", "display_cleanup_errno", "input_cleanup_errno", "core_live_bytes_after_close"):
        need(report.get(key) == 0, "cleanup error: " + key)
    need(report.get("input_frames", 0) > 10 and report.get("presents", 0) > 1, "missing physical input/display work")
    need(report.get("wall_seconds", 0) > 0 and report.get("peak_rss_kib", 0) > 0, "runtime usage evidence missing")


def coffee_gate(report):
    need(report.get("page_mask") == 15 and report.get("modal_seen") == 1 and report.get("completed", 0) >= 1,
         "Coffee navigation flow incomplete")
    count = report.get("item_count")
    need(type(count) is int and count in (8, 100), "unknown Coffee catalog workload")
    need(0 < report.get("pool", 0) <= min(12, count), "virtual pool bound missing")
    need(report.get("peak_pool", 99) <= 12 and 0 < report.get("nodes", 0) <= 64, "virtual node bound missing")
    if count > 12:
        need(report.get("recycled", 0) >= 2, "large-catalog recycling missing")
    need(report.get("media_applied", 0) >= 1, "dynamic media update was not exercised")
    need(report.get("disconnects", 0) >= 1 and report.get("reconnects", 0) >= 1 and report.get("syn_dropped", 0) >= 1,
         "controlled input fault/recovery evidence missing")
    need(report.get("text_input_open") is not True, "Coffee evidence ended with editor open")


def input_gate(report, manifest):
    packages = manifest.get("packages")
    need(type(packages) is dict and digest(packages.get("coffee-ime.pui")), "manifest lacks coffee-ime.pui")
    need(report.get("package_admitted") is True, "IME package was not admitted")
    need(report.get("package_authenticated") is False, "unsigned development IME must not claim publisher authentication")
    need(report.get("package_sha256") == packages["coffee-ime.pui"], "wrong IME package")
    need(report.get("application_capabilities") == 15, "wrong IME capability mask")
    need(report.get("text_input_open") is False, "input evidence ended with editor open")
    need(report.get("text_input_opens", 0) >= 2, "confirm/cancel input cycles not exercised")
    need(report.get("text_input_confirms", 0) >= 1, "text input confirm path missing")
    need(report.get("text_input_cancels", 0) >= 1, "text input cancel path missing")
    need(report.get("ime_commits", 0) >= 1, "real candidate commit missing")
    need(report.get("ime_candidate_batches", 0) >= 1, "real candidate generation missing")


def input_device_gate(data, profile):
    need(data.get("schema") == 1 and data.get("operation") == "live-input-admission", "wrong live-input report")
    need(data.get("admitted") is True and data.get("error") is None, "input device not admitted")
    need(data.get("errno") == 0 and data.get("cleanup_errno") == 0, "input device error")
    # Native diagnostics deliberately never self-certify physical orientation.
    need(data.get("orientation_verified") is False, "automatic input report must not self-approve orientation")
    expected = data.get("expected")
    need(type(expected) is dict, "input expected transform missing")
    if profile == "imx6ul-1024x800":
        need(type(expected.get("name")) is str and expected["name"] not in ("", "auto"),
             "800 target must bind an explicitly reviewed touch controller")
    axes = data.get("axes")
    need(type(axes) is dict and axes.get("raw_x_max", 0) > axes.get("raw_x_min", 0)
         and axes.get("raw_y_max", 0) > axes.get("raw_y_min", 0), "invalid touch axes")


def timeline_gate(path: Path, require_virtualization: bool):
    rows = list(csv.DictReader(path.open()))
    need(rows, "timeline is empty")
    if require_virtualization:
        need("first" in rows[0], "timeline lacks virtual-list index")
        need(any(int(row["first"]) > 0 for row in rows), "no visible virtual-window transition")


def verify_review(profile_dir: Path, manifest, report_hashes):
    review = load(profile_dir / "review.json")
    need(review.get("schema") == 2, "R6 review schema must be 2")
    need(review.get("approved") is True, "human approval missing")
    need(isinstance(review.get("reviewer"), str) and review["reviewer"].strip(), "reviewer missing")
    need(isinstance(review.get("device_id"), str) and review["device_id"].strip(), "lab device id missing")
    need(review.get("package_binary_sha256") == manifest["binary_sha256"], "review not bound to Runtime ELF")
    need(review.get("reports") == report_hashes, "review not bound to exact Coffee/input reports")
    checks = review.get("checks")
    need(type(checks) is dict and set(checks) == set(HUMAN_CHECKS), "human check set incomplete/expanded")
    need(all(checks[name] is True for name in HUMAN_CHECKS), "human functional review incomplete")
    evidence = review.get("evidence")
    need(type(evidence) is dict and evidence, "review evidence map missing")
    required = {
        f"{scenario}/{name}"
        for scenario in SCENARIOS
        for name in IMPORTANT_FILES
    }
    need(required <= set(evidence), "important board evidence not review-bound")
    for scenario in SCENARIOS:
        need(any(name.startswith(scenario + "/") and Path(name).suffix.lower() in VIDEO_SUFFIXES for name in evidence),
             scenario + " LCD/finger video missing")
    for name, expected in evidence.items():
        need(digest(expected), "invalid review digest: " + name)
        path = member(profile_dir, name)
        need(path.is_file() and path.stat().st_size > 0, "review evidence missing: " + name)
        need(sha(path) == expected, "review evidence changed: " + name)


def verify(report_root: Path, manifest_path: Path):
    manifest = load(manifest_path)
    need(manifest.get("schema") == 1, "wrong package manifest schema")
    need(manifest.get("profiles") == list(PROFILES), "package target list mismatch")
    need(digest(manifest.get("binary_sha256")), "invalid package Runtime digest")
    need(type(manifest.get("source_commit")) is str and re.fullmatch(r"[0-9a-f]{40}", manifest["source_commit"]),
         "invalid source commit")
    need(manifest.get("physical_hardware") is False, "software build manifest must not claim physical acceptance")
    for profile in PROFILES:
        profile_dir = report_root / profile
        need(profile_dir.is_dir() and not profile_dir.is_symlink(), "missing target evidence: " + profile)
        report_hashes = {}
        for scenario in SCENARIOS:
            run = profile_dir / scenario
            need(run.is_dir() and not run.is_symlink(), "missing scenario: " + profile + "/" + scenario)
            verify_run_sums(run)
            report = load(run / "runtime/report.json")
            common_runtime_gate(report, profile, manifest)
            input_device_gate(load(run / "runtime/input.json"), profile)
            load(run / "runtime/display.json")
            timeline_gate(run / "runtime/timeline.csv", scenario == "coffee")
            if scenario == "coffee":
                coffee_gate(report)
            else:
                input_gate(report, manifest)
            report_hashes[scenario] = sha(run / "runtime/report.json")
        verify_review(profile_dir, manifest, report_hashes)
    print("R6_BOARD_EVIDENCE_CONSISTENT both-targets coffee+input human-attested; physical authenticity remains human responsibility")


def write_json(path: Path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n")


def fixture(root: Path):
    manifest = {
        "schema": 1,
        "source_commit": "a" * 40,
        "source_tree": "d" * 40,
        "profiles": list(PROFILES),
        "binary": "ui-framework",
        "binary_sha256": "b" * 64,
        "static": True,
        "physical_hardware": False,
        "packages": {"coffee-ime.pui": "c" * 64},
    }
    write_json(root / "manifest.json", manifest)
    for profile in PROFILES:
        profile_dir = root / "returned" / profile
        profile_dir.mkdir(parents=True)
        report_hashes = {}
        evidence = {}
        for scenario in SCENARIOS:
            run = profile_dir / scenario
            runtime = run / "runtime"
            runtime.mkdir(parents=True)
            common = {
                "schema": 1, "commit": manifest["source_commit"], "profile": profile,
                "ok": True, "physical_io": True, "synthetic_input": False,
                "visual_validated": False, "business_commands": False, "error": None,
                "package_authenticated": False, "unblank_errno": 0,
                "display_cleanup_errno": 0, "input_cleanup_errno": 0,
                "core_live_bytes_after_close": 0, "input_frames": 100, "presents": 20,
                "wall_seconds": 180.0, "peak_rss_kib": 6000, "text_input_open": False,
            }
            if scenario == "coffee":
                common.update({
                    "page_mask": 15, "modal_seen": 1, "completed": 1,
                    "item_count": 100, "pool": 12, "peak_pool": 12, "nodes": 44,
                    "recycled": 20, "media_applied": 1, "disconnects": 1,
                    "reconnects": 1, "syn_dropped": 1,
                })
            else:
                common.update({
                    "package_admitted": True, "package_sha256": manifest["packages"]["coffee-ime.pui"],
                    "application_capabilities": 15, "text_input_opens": 3,
                    "text_input_confirms": 1, "text_input_cancels": 1,
                    "ime_commits": 1, "ime_candidate_batches": 2,
                })
            write_json(runtime / "report.json", common)
            write_json(runtime / "display.json", {"schema": 1, "ok": True})
            expected_name = "goodix-ts" if profile.endswith("600") else "reviewed-800-touch"
            write_json(runtime / "input.json", {
                "schema": 1, "operation": "live-input-admission", "admitted": True,
                "error": None, "errno": 0, "cleanup_errno": 0, "orientation_verified": False,
                "expected": {"name": expected_name, "swap_xy": 0, "invert_x": 0, "invert_y": 0},
                "axes": {"raw_x_min": 0, "raw_x_max": 1024, "raw_y_min": 0,
                         "raw_y_max": 600 if profile.endswith("600") else 800},
            })
            (runtime / "timeline.csv").write_text("first\n0\n1\n" if scenario == "coffee" else "first\n0\n")
            (runtime / "first.ppm").write_bytes(b"P6\n1 1\n255\n\x00\x00\x00")
            (runtime / "last.ppm").write_bytes(b"P6\n1 1\n255\n\x01\x01\x01")
            (run / "startup.log").write_text("R6 synthetic unit fixture; not hardware\n")
            files = [p for p in run.rglob("*") if p.is_file() and p.name != "SHA256SUMS"]
            (run / "SHA256SUMS").write_text("".join(f"{sha(p)}  ./{p.relative_to(run).as_posix()}\n" for p in sorted(files)))
            video = run / ("actual-" + scenario + ".mp4")
            video.write_bytes(b"synthetic-test-video")
            for name in IMPORTANT_FILES:
                relative = f"{scenario}/{name}"
                evidence[relative] = sha(profile_dir / relative)
            evidence[f"{scenario}/{video.name}"] = sha(video)
            report_hashes[scenario] = sha(runtime / "report.json")
        write_json(profile_dir / "review.json", {
            "schema": 2, "reviewer": "unit-reviewer", "device_id": "lab-" + profile,
            "approved": True, "package_binary_sha256": manifest["binary_sha256"],
            "reports": report_hashes,
            "checks": {name: True for name in HUMAN_CHECKS},
            "evidence": evidence,
        })
    return root / "returned", root / "manifest.json"


class R6GateTests(unittest.TestCase):
    def make(self):
        temp = tempfile.TemporaryDirectory()
        root = Path(temp.name)
        report, manifest = fixture(root)
        return temp, root, report, manifest

    def test_positive(self):
        temp, _, report, manifest = self.make()
        with temp:
            verify(report, manifest)

    def test_missing_input_scenario(self):
        temp, root, report, manifest = self.make()
        with temp:
            import shutil
            shutil.rmtree(report / PROFILES[0] / "input")
            with self.assertRaisesRegex(EvidenceError, "missing scenario"):
                verify(report, manifest)

    def test_headless_rejected(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "coffee/runtime/report.json"
            data = load(p); data["physical_io"] = False; write_json(p, data)
            with self.assertRaisesRegex(EvidenceError, "physical"):
                verify(report, manifest)

    def test_missing_ime_commit_rejected(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "input/runtime/report.json"
            data = load(p); data["ime_commits"] = 0; write_json(p, data)
            with self.assertRaises(EvidenceError):
                verify(report, manifest)

    def test_wrong_ime_package_rejected(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "input/runtime/report.json"
            data = load(p); data["package_sha256"] = "e" * 64; write_json(p, data)
            with self.assertRaises(EvidenceError):
                verify(report, manifest)

    def test_800_auto_controller_rejected(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[1] / "coffee/runtime/input.json"
            data = load(p); data["expected"]["name"] = "auto"; write_json(p, data)
            with self.assertRaisesRegex(EvidenceError, "800"):
                verify(report, manifest)

    def test_fault_recovery_required(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "coffee/runtime/report.json"
            data = load(p); data["syn_dropped"] = 0; write_json(p, data)
            with self.assertRaisesRegex(EvidenceError, "fault"):
                verify(report, manifest)

    def test_human_check_required(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "review.json"
            data = load(p); data["checks"]["pinyin_candidate_commit"] = False; write_json(p, data)
            with self.assertRaisesRegex(EvidenceError, "human"):
                verify(report, manifest)

    def test_video_required(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "review.json"
            data = load(p)
            for name in list(data["evidence"]):
                if name.startswith("input/") and Path(name).suffix == ".mp4":
                    del data["evidence"][name]
            write_json(p, data)
            with self.assertRaisesRegex(EvidenceError, "video"):
                verify(report, manifest)

    def test_run_hash_manifest_required(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "coffee/SHA256SUMS"
            p.write_text("")
            with self.assertRaises(EvidenceError):
                verify(report, manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(R6GateTests)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        return 0 if result.wasSuccessful() else 1
    need(args.report is not None and args.manifest is not None, "--report and --manifest are required")
    verify(args.report.resolve(), args.manifest.resolve())
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, EvidenceError, KeyError, TypeError, json.JSONDecodeError) as exc:
        print("R6_BOARD_PENDING_OR_REJECTED:", str(exc), file=sys.stderr)
        sys.exit(1)
