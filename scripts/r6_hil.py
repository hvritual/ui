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
import math
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


def no_symlinks(path: Path):
    need(not any(p.is_symlink() for p in (path, *path.parents)),
         "symlink evidence rejected: " + str(path))


def finite_constant(value):
    raise EvidenceError("non-finite JSON number: " + value)


def load(path: Path):
    no_symlinks(path)
    need(path.is_file() and path.stat().st_size <= 16 * 1024 * 1024,
         "missing/oversized JSON: " + str(path))
    result = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique,
                        parse_constant=finite_constant)
    need(type(result) is dict, "JSON evidence root must be an object")
    return result


def sha(path: Path):
    no_symlinks(path)
    need(path.is_file(), "non-regular evidence file: " + str(path))
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def member(root: Path, relative: str):
    need(type(relative) is str and relative and "\\" not in relative
         and not any(ord(c) < 32 for c in relative), "invalid evidence path")
    p = Path(relative)
    need(not p.is_absolute() and ".." not in p.parts and p.as_posix() == relative
         and relative != ".", "unsafe evidence path: " + relative)
    target = root / p
    no_symlinks(target)
    need(target.resolve().is_relative_to(root.resolve()), "evidence escaped root")
    return target


def integer(value, minimum=0):
    return type(value) is int and value >= minimum


def positive_number(value):
    return type(value) in (int, float) and math.isfinite(value) and value > 0


def digest(value):
    return type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def verify_run_sums(run: Path, required=IMPORTANT_FILES):
    sums = member(run, "SHA256SUMS")
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
    for name in required:
        need(name in entries, "run manifest missing required file: " + name)
    return entries


def common_runtime_gate(report, profile, manifest):
    need(type(report.get("schema")) is int and report["schema"] == 1, "wrong runtime schema")
    need(report.get("profile") == profile, "wrong runtime profile")
    need(report.get("commit") == manifest["source_commit"], "runtime source commit mismatch")
    need(report.get("ok") is True and report.get("error") is None, "runtime failed")
    need(report.get("physical_io") is True, "physical I/O not evidenced")
    need(report.get("synthetic_input") is False and report.get("synthetic", False) is False, "synthetic run cannot pass R6")
    need(report.get("visual_validated") is False, "automatic report must not claim visual validation")
    need(report.get("business_commands") is False, "business/device commands must remain disabled")
    need(report.get("replay_realtime") is False and type(report.get("replay_samples")) is int
         and report["replay_samples"] == 0, "replay evidence cannot pass R6")
    need(report.get("package_authenticated") is False, "publisher authentication overclaimed")
    for key in ("input_errno", "unblank_errno", "display_cleanup_errno", "input_cleanup_errno", "core_live_bytes_after_close"):
        need(type(report.get(key)) is int and report[key] == 0, "cleanup error: " + key)
    need(integer(report.get("input_frames"), 11) and integer(report.get("presents"), 2), "missing physical input/display work")
    need(positive_number(report.get("wall_seconds")) and integer(report.get("peak_rss_kib"), 1), "runtime usage evidence missing")
    for key in ("completed", "pool", "peak_pool", "nodes", "recycled", "media_applied",
                "disconnects", "reconnects", "syn_dropped", "text_input_opens",
                "text_input_confirms", "text_input_cancels", "ime_commits", "ime_candidate_batches"):
        need(integer(report.get(key)), "invalid runtime counter: " + key)


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
    need(report.get("text_input_open") is False, "Coffee evidence ended with editor open")
    need(report.get("package_admitted") is False and report.get("package_sha256") is None,
         "Coffee closure must use the documented directory scenario")


def input_gate(report, manifest):
    packages = manifest.get("packages")
    need(type(packages) is dict and digest(packages.get("coffee-ime.pui")), "manifest lacks coffee-ime.pui")
    need(report.get("package_admitted") is True, "IME package was not admitted")
    need(report.get("package_authenticated") is False, "unsigned development IME must not claim publisher authentication")
    need(report.get("package_sha256") == packages["coffee-ime.pui"], "wrong IME package")
    need(type(report.get("application_capabilities")) is int and report["application_capabilities"] == 15, "wrong IME capability mask")
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
    width, height = 1024, int(profile.rsplit("x", 1)[1])
    need(expected.get("width") == width and expected.get("height") == height,
         "input target dimensions mismatch")
    for name in ("swap_xy", "invert_x", "invert_y"):
        need(type(expected.get(name)) is int and expected[name] in (0, 1), "missing/invalid input transform")
    need(type(data.get("name")) is str and data["name"].strip(), "input controller identity missing")
    if expected.get("name") != "auto":
        need(data["name"] == expected.get("name"), "input controller binding mismatch")
    need(data.get("protocol") in ("mt-a", "mt-b") and data.get("axes_queried") is True,
         "input protocol/probe missing")
    axes = data.get("axes")
    need(type(axes) is dict and all(type(axes.get(k)) is int for k in
         ("raw_x_min", "raw_x_max", "raw_y_min", "raw_y_max")), "invalid touch axes")
    need(type(axes) is dict and axes.get("raw_x_max", 0) > axes.get("raw_x_min", 0)
         and axes.get("raw_y_max", 0) > axes.get("raw_y_min", 0), "invalid touch axes")


def display_gate(data, profile):
    need(type(data.get("schema_version")) is int and data["schema_version"] == 1,
         "wrong display schema")
    need(data.get("observed") is True and data.get("admitted") is True
         and data.get("operation") == "display-test" and data.get("error") is None,
         "display probe failed/not writable")
    need(data.get("errno") == 0 and data.get("cleanup_errno") == 0, "display probe error")
    need(data.get("physical_panel_validated") is False and data.get("mode_changed_by_host") is False,
         "display report overclaims physical/mode acceptance")
    info = data.get("info")
    need(type(info) is dict, "display layout missing")
    for key in ("xres", "yres", "xres_virtual", "yres_virtual", "xoffset", "yoffset",
                "bits_per_pixel", "line_length", "smem_len"):
        need(integer(info.get(key)), "invalid display layout: " + key)
    need(info["xres"] == 1024 and info["yres"] == int(profile.rsplit("x", 1)[1]),
         "display target dimensions mismatch")
    need(info["bits_per_pixel"] in (16, 32), "unsupported display pixel width")
    width, height, stride = info["xres"], info["yres"], info["line_length"]
    x, y, pixel = info["xoffset"], info["yoffset"], info["bits_per_pixel"] // 8
    need(x + width <= info["xres_virtual"] and y + height <= info["yres_virtual"]
         and (x + width) * pixel <= stride
         and (y + height - 1) * stride + (x + width) * pixel <= info["smem_len"],
         "display layout out of bounds")


def ppm_gate(path: Path, profile):
    no_symlinks(path)
    with path.open("rb") as stream:
        need(stream.readline(32) == b"P6\n", "invalid snapshot format")
        size = stream.readline(64).strip().split()
        need(size == [b"1024", profile.rsplit("x", 1)[1].encode()], "snapshot target dimensions mismatch")
        need(stream.readline(32) == b"255\n", "invalid snapshot depth")
        need(path.stat().st_size - stream.tell() == 1024 * int(size[1]) * 3, "snapshot payload length mismatch")


def startup_gate(path: Path, profile, scenario, manifest):
    need(path.stat().st_size <= 16 * 1024 * 1024, "oversized startup log")
    text = path.read_text(encoding="utf-8")
    need(text.count("F6A_DIAGNOSTIC_BEGIN profile=" + profile) == 1, "startup profile mismatch")
    need(text.count("=== PROGRAM HASH ===\n") == 1 and text.count("=== BUILD MANIFEST ===\n") == 1,
         "startup identity markers missing/duplicated")
    block = text.split("=== PROGRAM HASH ===\n", 1)[1].split("=== BUILD MANIFEST ===\n", 1)[0].strip()
    match = re.fullmatch(r"([0-9a-f]{64})  (.+/)?ui-framework", block)
    need(match is not None and match[1] == manifest["binary_sha256"], "startup Runtime hash mismatch")
    block = text.split("=== BUILD MANIFEST ===\n", 1)[1].lstrip()
    observed, _ = json.JSONDecoder(object_pairs_hook=unique, parse_constant=finite_constant).raw_decode(block)
    need(observed == manifest, "startup build manifest mismatch")
    application = "coffee-ime" if scenario == "input" else "coffee"
    need(re.findall(r"^APPLICATION=([^;]+);", text, re.M) == [application], "startup application mismatch")
    need(re.findall(r"^FRAMEWORK_PROCESS_EXIT=(.*)$", text, re.M) == ["0"], "startup process exit missing/failed")


def approved_bundle(manifest_path):
    manifest = load(manifest_path)
    need(manifest.get("schema") == 1 and manifest.get("profiles") == list(PROFILES), "invalid approved bundle")
    need(manifest.get("binary") == "ui-framework" and manifest.get("static") is True,
         "approved static Runtime required")
    need(manifest.get("physical_hardware") is False, "software build must not claim physical acceptance")
    for key in ("source_commit", "source_tree"):
        need(type(manifest.get(key)) is str and re.fullmatch(r"[0-9a-f]{40}", manifest[key]), "invalid bundle source identity")
    root = manifest_path.parent
    packages = manifest.get("packages")
    need(type(packages) is dict and "coffee-ime.pui" in packages, "approved IME package missing")
    required = [manifest_path.name, "ui-framework"]
    need(digest(manifest.get("binary_sha256")) and sha(member(root, "ui-framework")) == manifest["binary_sha256"],
         "approved Runtime bytes mismatch")
    for name, expected in packages.items():
        need(type(name) is str and re.fullmatch(r"[a-z0-9-]+\.pui", name), "invalid package name")
        path = "packages/" + name
        need(digest(expected) and sha(member(root, path)) == expected, "approved package bytes mismatch")
        required.append(path)
    verify_run_sums(root, required)
    return manifest


def timeline_gate(path: Path, report, require_virtualization: bool):
    previous = -1
    moved = False
    count = 0
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        need(reader.fieldnames and len(set(reader.fieldnames)) == len(reader.fieldnames)
             and {"sample_ns", "presents", "first"} <= set(reader.fieldnames), "timeline header invalid")
        for row in reader:
            need(None not in row and all(re.fullmatch(r"[0-9]+", row[k] or "")
                 for k in ("sample_ns", "presents", "first")), "invalid timeline record")
            sample = int(row["sample_ns"])
            need(sample >= previous, "timeline timestamps not monotonic")
            need(int(row["presents"]) <= report["presents"], "timeline/report presents mismatch")
            previous = sample
            moved |= int(row["first"]) > 0
            count += 1
    need(count > 0, "timeline is empty")
    if require_virtualization:
        need(moved, "no visible virtual-window transition")


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


def verify(report_root: Path, manifest_path: Path, *, emit=True):
    no_symlinks(report_root)
    manifest = approved_bundle(manifest_path)
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
        controllers = []
        for scenario in SCENARIOS:
            run = profile_dir / scenario
            need(run.is_dir() and not run.is_symlink(), "missing scenario: " + profile + "/" + scenario)
            verify_run_sums(run)
            report = load(run / "runtime/report.json")
            common_runtime_gate(report, profile, manifest)
            data = load(run / "runtime/input.json")
            input_device_gate(data, profile)
            controllers.append({k: data[k] for k in ("name", "protocol", "expected", "axes")})
            display_gate(load(run / "runtime/display.json"), profile)
            startup_gate(run / "startup.log", profile, scenario, manifest)
            timeline_gate(run / "runtime/timeline.csv", report, scenario == "coffee")
            for name in ("first.ppm", "last.ppm"):
                ppm_gate(run / "runtime" / name, profile)
            if scenario == "coffee":
                coffee_gate(report)
            else:
                input_gate(report, manifest)
            report_hashes[scenario] = sha(run / "runtime/report.json")
        need(controllers[0] == controllers[1], "Coffee/IME controller or transform differs")
        verify_review(profile_dir, manifest, report_hashes)
    if emit:
        print("R6_BOARD_EVIDENCE_CONSISTENT both-targets coffee+input human-attested; physical authenticity remains human responsibility")


def write_json(path: Path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n")


def refresh_sums(run: Path):
    files = [p for p in run.rglob("*") if p.is_file() and p.name != "SHA256SUMS"
             and p.suffix.lower() not in VIDEO_SUFFIXES]
    (run / "SHA256SUMS").write_text(
        "".join(f"{sha(p)}  ./{p.relative_to(run).as_posix()}\n" for p in sorted(files))
    )


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
    (root / "ui-framework").write_bytes(b"synthetic Runtime fixture, NOT an executable")
    (root / "packages").mkdir()
    (root / "packages/coffee-ime.pui").write_bytes(b"synthetic package, NOT deployable")
    manifest["binary_sha256"] = sha(root / "ui-framework")
    manifest["packages"]["coffee-ime.pui"] = sha(root / "packages/coffee-ime.pui")
    write_json(root / "manifest.json", manifest)
    refresh_sums(root)
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
                "input_errno": 0, "replay_samples": 0, "replay_realtime": False,
                "completed": 0, "pool": 0, "peak_pool": 0, "nodes": 0, "recycled": 0,
                "media_applied": 0, "disconnects": 0, "reconnects": 0, "syn_dropped": 0,
                "text_input_opens": 0, "text_input_confirms": 0, "text_input_cancels": 0,
                "ime_commits": 0, "ime_candidate_batches": 0,
                "package_admitted": False, "package_sha256": None,
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
            height = int(profile.rsplit("x", 1)[1])
            write_json(runtime / "display.json", {
                "schema_version": 1, "observed": True, "admitted": True,
                "operation": "display-test", "error": None, "errno": 0, "cleanup_errno": 0,
                "physical_panel_validated": False, "mode_changed_by_host": False,
                "info": {"xres": 1024, "yres": height, "xres_virtual": 1024, "yres_virtual": height,
                         "xoffset": 0, "yoffset": 0, "bits_per_pixel": 32,
                         "line_length": 4096, "smem_len": 4096 * height}})
            expected_name = "goodix-ts" if profile.endswith("600") else "reviewed-800-touch"
            write_json(runtime / "input.json", {
                "schema": 1, "operation": "live-input-admission", "admitted": True,
                "error": None, "errno": 0, "cleanup_errno": 0, "orientation_verified": False,
                "name": expected_name, "protocol": "mt-a", "axes_queried": True,
                "expected": {"name": expected_name, "width": 1024, "height": height,
                             "swap_xy": 0, "invert_x": 0, "invert_y": 0},
                "axes": {"raw_x_min": 0, "raw_x_max": 1024, "raw_y_min": 0,
                         "raw_y_max": 600 if profile.endswith("600") else 800},
            })
            (runtime / "timeline.csv").write_text("sample_ns,presents,first\n1,1,0\n2,2," + ("1" if scenario == "coffee" else "0") + "\n")
            header = f"P6\n1024 {height}\n255\n".encode()
            (runtime / "first.ppm").write_bytes(header + b"\x00" * (1024 * height * 3))
            (runtime / "last.ppm").write_bytes(header + b"\x01" * (1024 * height * 3))
            app = "coffee-ime" if scenario == "input" else "coffee"
            (run / "startup.log").write_text(
                "SYNTHETIC UNIT FIXTURE; NOT HARDWARE\nF6A_DIAGNOSTIC_BEGIN profile=" + profile +
                "\n=== PROGRAM HASH ===\n" + manifest["binary_sha256"] + "  /lab/ui-framework\n" +
                "=== BUILD MANIFEST ===\n" + json.dumps(manifest) + "\nAPPLICATION=" + app +
                "; unsigned IME development diagnostic\nFRAMEWORK_PROCESS_EXIT=0\n")
            refresh_sums(run)
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
            verify(report, manifest, emit=False)

    def test_missing_input_scenario(self):
        temp, root, report, manifest = self.make()
        with temp:
            import shutil
            shutil.rmtree(report / PROFILES[0] / "input")
            with self.assertRaisesRegex(EvidenceError, "missing scenario"):
                verify(report, manifest, emit=False)

    def test_headless_rejected(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "coffee/runtime/report.json"
            data = load(p); data["physical_io"] = False; write_json(p, data)
            refresh_sums(report / PROFILES[0] / "coffee")
            with self.assertRaisesRegex(EvidenceError, "physical"):
                verify(report, manifest, emit=False)

    def test_missing_ime_commit_rejected(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "input/runtime/report.json"
            data = load(p); data["ime_commits"] = 0; write_json(p, data)
            refresh_sums(report / PROFILES[0] / "input")
            with self.assertRaisesRegex(EvidenceError, "candidate commit"):
                verify(report, manifest, emit=False)

    def test_wrong_ime_package_rejected(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "input/runtime/report.json"
            data = load(p); data["package_sha256"] = "e" * 64; write_json(p, data)
            refresh_sums(report / PROFILES[0] / "input")
            with self.assertRaisesRegex(EvidenceError, "wrong IME package"):
                verify(report, manifest, emit=False)

    def test_800_auto_controller_rejected(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[1] / "coffee/runtime/input.json"
            data = load(p); data["expected"]["name"] = "auto"; write_json(p, data)
            refresh_sums(report / PROFILES[1] / "coffee")
            with self.assertRaisesRegex(EvidenceError, "800"):
                verify(report, manifest, emit=False)

    def test_fault_recovery_required(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "coffee/runtime/report.json"
            data = load(p); data["syn_dropped"] = 0; write_json(p, data)
            refresh_sums(report / PROFILES[0] / "coffee")
            with self.assertRaisesRegex(EvidenceError, "fault"):
                verify(report, manifest, emit=False)

    def test_human_check_required(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "review.json"
            data = load(p); data["checks"]["pinyin_candidate_commit"] = False; write_json(p, data)
            with self.assertRaisesRegex(EvidenceError, "human"):
                verify(report, manifest, emit=False)

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
                verify(report, manifest, emit=False)

    def test_run_hash_manifest_required(self):
        temp, _, report, manifest = self.make()
        with temp:
            p = report / PROFILES[0] / "coffee/SHA256SUMS"
            p.write_text("")
            with self.assertRaises(EvidenceError):
                verify(report, manifest, emit=False)


    def rejected_change(self, relative, mutate, message, profile=PROFILES[0], scenario="coffee"):
        """Reseal ONLY a temporary synthetic fixture to reach the intended gate."""
        temp, _, reports, manifest = self.make()
        with temp:
            run = reports / profile / scenario
            path = run / relative
            mutate(path)
            refresh_sums(run)
            with self.assertRaisesRegex(EvidenceError, message):
                verify(reports, manifest, emit=False)

    def test_wrong_display_resolution(self):
        def change(p):
            d = load(p); d["info"]["yres"] = 800; write_json(p, d)
        self.rejected_change("runtime/display.json", change, "display target")

    def test_failed_display_probe(self):
        def change(p):
            d = load(p); d["admitted"] = False; write_json(p, d)
        self.rejected_change("runtime/display.json", change, "display probe")

    def test_display_memory_overflow(self):
        def change(p):
            d = load(p); d["info"]["smem_len"] = 1; write_json(p, d)
        self.rejected_change("runtime/display.json", change, "out of bounds")

    def test_padded_rgb565_display_allowed(self):
        d = {"schema_version": 1, "observed": True, "admitted": True, "operation": "display-test",
             "error": None, "errno": 0, "cleanup_errno": 0, "physical_panel_validated": False,
             "mode_changed_by_host": False, "info": {"xres": 1024, "yres": 600, "xres_virtual": 1030,
             "yres_virtual": 602, "xoffset": 2, "yoffset": 1, "bits_per_pixel": 16,
             "line_length": 2064, "smem_len": 2064*602}}
        display_gate(d, PROFILES[0])

    def test_wrong_elf_in_startup(self):
        def change(p):
            text = p.read_text(); start = text.index("=== PROGRAM HASH ===\n") + len("=== PROGRAM HASH ===\n")
            p.write_text(text[:start] + "e"*64 + text[start+64:])
        self.rejected_change("startup.log", change, "Runtime hash")

    def test_mismatched_embedded_manifest(self):
        def change(p):
            p.write_text(p.read_text().replace('"source_tree": "' + "d"*40, '"source_tree": "' + "f"*40))
        self.rejected_change("startup.log", change, "build manifest")

    def test_wrong_startup_application(self):
        def change(p):
            p.write_text(p.read_text().replace("APPLICATION=coffee;", "APPLICATION=coffee-ime;"))
        self.rejected_change("startup.log", change, "application mismatch")

    def test_failed_process_exit(self):
        def change(p):
            p.write_text(p.read_text().replace("FRAMEWORK_PROCESS_EXIT=0", "FRAMEWORK_PROCESS_EXIT=1"))
        self.rejected_change("startup.log", change, "process exit")

    def test_missing_explicit_synthetic_flag(self):
        def change(p):
            d = load(p); del d["synthetic_input"]; write_json(p, d)
        self.rejected_change("runtime/report.json", change, "synthetic")

    def test_integer_synthetic_flag(self):
        def change(p):
            d = load(p); d["synthetic_input"] = 1; write_json(p, d)
        self.rejected_change("runtime/report.json", change, "synthetic")

    def test_replay_counter_rejected(self):
        def change(p):
            d = load(p); d["replay_samples"] = 12; write_json(p, d)
        self.rejected_change("runtime/report.json", change, "replay")

    def test_boolean_counter_rejected(self):
        def change(p):
            d = load(p); d["ime_commits"] = True; write_json(p, d)
        self.rejected_change("runtime/report.json", change, "counter", scenario="input")

    def test_nonfinite_json_rejected(self):
        def change(p):
            p.write_text(p.read_text().replace('"wall_seconds": 180.0', '"wall_seconds": Infinity'))
        self.rejected_change("runtime/report.json", change, "non-finite")

    def test_duplicate_json_rejected(self):
        def change(p):
            p.write_text(p.read_text().replace('"ok": true', '"ok": true, "ok": true'))
        self.rejected_change("runtime/report.json", change, "duplicate JSON")

    def test_wrong_snapshot_dimensions(self):
        def change(p): p.write_bytes(b"P6\n1 1\n255\n" + b"0"*3)
        self.rejected_change("runtime/first.ppm", change, "snapshot target")

    def test_truncated_snapshot(self):
        def change(p): p.write_bytes(p.read_bytes()[:-1])
        self.rejected_change("runtime/last.ppm", change, "payload length")

    def test_nonmonotonic_timeline(self):
        def change(p): p.write_text("sample_ns,presents,first\n2,1,0\n1,2,1\n")
        self.rejected_change("runtime/timeline.csv", change, "monotonic")

    def test_wrong_timeline_present_count(self):
        def change(p): p.write_text("sample_ns,presents,first\n1,999,1\n")
        self.rejected_change("runtime/timeline.csv", change, "presents mismatch")

    def test_input_transform_missing(self):
        def change(p):
            d = load(p); del d["expected"]["invert_y"]; write_json(p, d)
        self.rejected_change("runtime/input.json", change, "transform")

    def test_controller_differs_between_runs(self):
        def change(p):
            d = load(p); d["expected"]["name"] = "other-touch"; d["name"] = "other-touch"; write_json(p, d)
        self.rejected_change("runtime/input.json", change, "controller or transform differs", scenario="input")

    def test_symlink_parent_rejected(self):
        import shutil
        temp, root, report, manifest = self.make()
        with temp:
            path = report/PROFILES[0]/"coffee/runtime"
            outside = root/"outside"
            shutil.move(str(path), str(outside)); path.symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(EvidenceError, "symlink"):
                verify(report, manifest, emit=False)

    def test_approved_runtime_bytes_required(self):
        temp, root, report, manifest = self.make()
        with temp:
            (root/"ui-framework").write_bytes(b"wrong runtime")
            with self.assertRaisesRegex(EvidenceError, "Runtime bytes"):
                verify(report, manifest, emit=False)

    def test_approved_package_bytes_required(self):
        temp, root, report, manifest = self.make()
        with temp:
            (root/"packages/coffee-ime.pui").write_bytes(b"wrong package")
            with self.assertRaisesRegex(EvidenceError, "package bytes"):
                verify(report, manifest, emit=False)

    def test_changed_log_not_resealed(self):
        temp, _, report, manifest = self.make()
        with temp:
            (report/PROFILES[0]/"coffee/startup.log").write_text("modified")
            with self.assertRaisesRegex(EvidenceError, "digest mismatch"):
                verify(report, manifest, emit=False)

    def test_paths_reject_alias_traversal_and_controls(self):
        with tempfile.TemporaryDirectory() as t:
            for name in ("../outside", "a/../b", "a/./b", "a//b", "/tmp/b", "a\\b", "a\nb", "."):
                with self.subTest(name=name), self.assertRaises(EvidenceError): member(Path(t), name)

    def test_missing_800_target(self):
        import shutil
        temp, _, report, manifest = self.make()
        with temp:
            shutil.rmtree(report/PROFILES[1])
            with self.assertRaisesRegex(EvidenceError, "missing target"):
                verify(report, manifest, emit=False)

    def test_human_approval_stays_required(self):
        temp, _, report, manifest = self.make()
        with temp:
            path = report/PROFILES[0]/"review.json"; data = load(path); data["approved"] = False; write_json(path, data)
            with self.assertRaisesRegex(EvidenceError, "human approval"):
                verify(report, manifest, emit=False)

    def test_review_report_hash_binding(self):
        temp, _, report, manifest = self.make()
        with temp:
            path = report/PROFILES[0]/"review.json"; data = load(path); data["reports"]["input"] = "e"*64; write_json(path, data)
            with self.assertRaisesRegex(EvidenceError, "exact Coffee/input"):
                verify(report, manifest, emit=False)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(R6GateTests)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        if result.wasSuccessful():
            print(f"R6_VERIFIER_SELF_TEST_OK cases={result.testsRun} physical=false")
        return 0 if result.wasSuccessful() else 1
    need(args.report is not None and args.manifest is not None, "--report and --manifest are required")
    verify(args.report.absolute(), args.manifest.absolute())
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError, csv.Error) as exc:
        print("R6_BOARD_PENDING_OR_REJECTED:", str(exc), file=sys.stderr)
        sys.exit(1)
