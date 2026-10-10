#!/usr/bin/env python3
"""Read-only R6 partial-return triage. Never grants physical/product admission."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Callable

import r6_hil as gate

ERRORS = (gate.EvidenceError, OSError, ValueError, KeyError, TypeError, OverflowError)
# No input strings, preedit, candidates, text hashes or arbitrary report fields.
COUNTERS = ("text_input_opens", "text_input_confirms", "text_input_cancels",
            "ime_commits", "ime_candidate_batches", "ime_provider_stage",
            "ime_provider_errno", "ime_provider_storage", "ime_provider_verify_mode",
            "media_applied", "media_rejected", "media_deferred",
            "disconnects", "reconnects", "syn_dropped", "page_mask")


def check(name: str, action: Callable[[], object], missing=()) -> dict:
    """Evaluate existing gate logic without rewriting evidence or prerequisites."""
    if missing:
        return {"check": name, "state": "pending", "missing": list(missing)}
    try:
        action()
    except ERRORS as exc:
        return {"check": name, "state": "rejected", "reason": str(exc)}
    return {"check": name, "state": "consistent"}


def state(checks: list[dict]) -> str:
    states = {item["state"] for item in checks}
    if "rejected" in states:
        return "rejected"
    return "pending" if "pending" in states else "consistent"


def scenario(root: Path, profile: str, name: str, manifest: dict) -> dict:
    row = {"profile": profile, "scenario": name, "checks": [], "counters": {}}
    checks = row["checks"]
    try:
        run = gate.member(root, profile + "/" + name)
        gate.need(not run.exists() or run.is_dir(), "scenario must be a directory")
        present = {n: gate.member(run, n).is_file()
                   for n in (*gate.IMPORTANT_FILES, "SHA256SUMS")}
    except ERRORS as exc:
        checks.append({"check": "paths", "state": "rejected", "reason": str(exc)})
        row["state"] = state(checks)
        return row

    def run_check(label, files, action):
        checks.append(check(label, action, [n for n in files if not present[n]]))

    run_check("required-original-files", tuple(present), lambda: None)
    # Check all entries already provided, even when other mandatory files are absent.
    run_check("checksums", ("SHA256SUMS",), lambda: gate.verify_run_sums(run, required=()))
    report_path = run / "runtime/report.json"
    data = None
    if present["runtime/report.json"]:
        try:
            data = gate.load(report_path)
            row["report_sha256"] = gate.sha(report_path)
            row["counters"] = {k: data[k] for k in COUNTERS if type(data.get(k)) is int}
        except ERRORS as exc:
            checks.append({"check": "report-json", "state": "rejected", "reason": str(exc)})
    if data is not None:
        checks.append(check("runtime", lambda: gate.common_runtime_gate(data, profile, manifest)))
        action = (lambda: gate.coffee_gate(data)) if name == "coffee" else (lambda: gate.input_gate(data, manifest))
        checks.append(check("scenario-completion", action))
        run_check("timeline", ("runtime/timeline.csv",),
                  lambda: gate.timeline_gate(run / "runtime/timeline.csv", data, name == "coffee"))
    else:
        checks.append({"check": "runtime", "state": "pending", "missing": ["readable runtime/report.json"]})
    run_check("startup-identity", ("startup.log",),
              lambda: gate.startup_gate(run / "startup.log", profile, name, manifest))
    run_check("display", ("runtime/display.json",),
              lambda: gate.display_gate(gate.load(run / "runtime/display.json"), profile))
    run_check("touch", ("runtime/input.json",),
              lambda: gate.input_device_gate(gate.load(run / "runtime/input.json"), profile))
    for filename in ("runtime/first.ppm", "runtime/last.ppm"):
        run_check(filename, (filename,), lambda f=filename: gate.ppm_gate(run / f, profile))
    row["state"] = state(checks)
    return row


def inspect(report_root: Path, manifest_path: Path) -> dict:
    gate.no_symlinks(report_root)
    manifest = gate.approved_bundle(manifest_path)
    result = {"schema": 1, "scope": "read-only-intake-not-acceptance",
              "board_accepted": False, "product_admitted": False,
              "source_commit": manifest["source_commit"], "source_tree": manifest["source_tree"],
              "runtime_sha256": manifest["binary_sha256"],
              "manifest_sha256": gate.sha(manifest_path), "runs": [], "reviews": []}
    for profile in gate.PROFILES:
        rows = [scenario(report_root, profile, name, manifest) for name in gate.SCENARIOS]
        result["runs"].extend(rows)
        hashes = {row["scenario"]: row["report_sha256"] for row in rows if "report_sha256" in row}
        review_dir = report_root / profile
        review_file = review_dir / "review.json"
        missing = []
        # lexists-like handling: dangling symlinks must be rejected, not marked missing.
        if not review_file.exists() and not review_file.is_symlink():
            missing.append(profile + "/review.json (named human review and evidence bindings)")
        missing += [profile + "/" + n + "/runtime/report.json" for n in gate.SCENARIOS if n not in hashes]
        record = check("human-review", lambda: gate.verify_review(review_dir, manifest, hashes), missing)
        record["profile"] = profile
        result["reviews"].append(record)

    checks = [{"state": row["state"]} for row in result["runs"]] + result["reviews"]
    overall = state(checks)
    if overall == "consistent":
        # ONLY the unchanged final verifier decides whole-matrix consistency.
        final = check("unchanged-r6-verifier", lambda: gate.verify(report_root, manifest_path, emit=False))
        overall = final["state"]
    else:
        final = {"check": "unchanged-r6-verifier", "state": "pending",
                 "reason": "Resolve listed gaps, then run the unchanged final verifier; do not sum runs or edit counters."}
    result["final_gate"] = final
    result["state"] = overall
    result["exit_code"] = {"consistent": 0, "pending": 2, "rejected": 1}[overall]
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", type=Path, help="New JSON outside both original evidence and bundle")
    args = parser.parse_args(argv)
    try:
        if args.output:
            gate.no_symlinks(args.output)
            target = args.output.resolve()
            for protected in (args.report.resolve(), args.manifest.parent.resolve()):
                gate.need(not target.is_relative_to(protected), "output must be outside original evidence and bundle")
            gate.need(not args.output.exists(), "output already exists; never overwrite evidence")
        result = inspect(args.report, args.manifest)
        payload = json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
        if args.output:
            with args.output.open("x", encoding="utf-8") as stream:
                stream.write(payload)
        else:
            print(payload, end="")
        return result["exit_code"]
    except ERRORS as exc:
        print("R6_INTAKE_REJECTED: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
