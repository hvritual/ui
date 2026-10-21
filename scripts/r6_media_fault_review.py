#!/usr/bin/env python3
"""Read-only R6 same-run media/fault evidence triage, NOT physical approval.

Validates only bounded metadata and exact hashes. The human reviewer still owns
LCD/video authenticity, the real fault-injection SOP and the original R6 gate.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import sys

import r6_hil as gate

ERRORS = (gate.EvidenceError, OSError, ValueError, KeyError, TypeError, OverflowError)
PROVENANCE = {
    "bundle_manifest_sha256": "manifest.json",
    "runtime_sha256": "ui-framework",
    "mediactl_sha256": "mediactl",
    "demo_key_sha256": "updates/demo.public",
    "demo_a_sha256": "updates/demo-a.zip",
    "demo_b_sha256": "updates/demo-b.zip",
}
MEDIA_SEQUENCE = (
    "UI_IDLE_READY", "A_REQUESTED", "A_VISIBLE_OPERATOR",
    "B_BUSY_BEGIN_OPERATOR", "B_BUSY_REQUESTED", "B_BUSY_DEFERRED_OPERATOR",
    "B_VISIBLE_OPERATOR", "ROLLBACK_BUSY_BEGIN_OPERATOR",
    "ROLLBACK_BUSY_REQUESTED", "ROLLBACK_BUSY_DEFERRED_OPERATOR",
    "A_RESTORED_OPERATOR",
)
FAULT_SEQUENCE = (
    "FAULT_SOP_ACK_OPERATOR", "DISCONNECT_WINDOW_OPERATOR", "RECONNECT_OPERATOR",
    "SYN_DROPPED_WINDOW_OPERATOR", "SYN_DROPPED_OPERATOR", "NO_GHOST_TAP_OPERATOR",
)
ALL_EVENTS = set(MEDIA_SEQUENCE + FAULT_SEQUENCE)
GENERATIONS = {"A_REQUESTED", "B_BUSY_REQUESTED", "ROLLBACK_BUSY_REQUESTED"}
TIMESTAMP = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z")


class Pending(Exception):
    pass


def load_small(path: Path, limit=16384) -> str:
    gate.no_symlinks(path)
    if not path.is_file():
        raise Pending(f"missing {path.name}")
    gate.need(path.stat().st_size <= limit, "oversized review receipt: " + path.name)
    return path.read_text(encoding="utf-8")


def provenance(bundle: Path, session: Path, manifest: dict) -> dict:
    raw = load_small(session / "provenance.tsv")
    record = {}
    for line in raw.splitlines():
        parts = line.split("\t")
        gate.need(len(parts) == 2 and parts[0] not in record, "malformed provenance")
        record[parts[0]] = parts[1]
    gate.need(set(record) == {"schema", "profile", *PROVENANCE}, "provenance keys incomplete/extra")
    gate.need(record["schema"] == "R6MF1" and record["profile"] in gate.PROFILES, "unsupported field lab receipt")
    gate.need(record["runtime_sha256"] == manifest["binary_sha256"], "wrong bundle Runtime identity")
    for key, filename in PROVENANCE.items():
        gate.need(gate.digest(record[key]), "invalid pinned digest: " + key)
        gate.need(gate.sha(gate.member(bundle, filename)) == record[key], "changed/mixed bundle: " + filename)
    return record


def actions(session: Path) -> list[tuple[str, str]]:
    lines = load_small(session / "actions.tsv", 8192).splitlines()
    gate.need(len(lines) <= len(ALL_EVENTS), "extra/duplicated field event")
    output = []
    past = None
    for line in lines:
        parts = line.split("\t")
        gate.need(len(parts) == 3 and TIMESTAMP.fullmatch(parts[0]), "malformed operator timestamp")
        timestamp = datetime.strptime(parts[0], "%Y-%m-%dT%H:%M:%SZ")
        gate.need(past is None or timestamp >= past, "non-monotonic operator sequence")
        past = timestamp
        _, event, value = parts
        gate.need(event in ALL_EVENTS and event not in (x[0] for x in output), "unknown/duplicate operator event")
        gate.need((gate.digest(value) if event in GENERATIONS else value == "-"),
                  "unsafe/unexpected field evidence content")
        output.append((event, value))
    return output


def stage_sequence(events: list[tuple[str, str]], required: tuple[str, ...]) -> dict:
    observed = [name for name, _ in events if name in required]
    gate.need(observed == list(required[:len(observed)]), "out-of-order field checkpoints")
    if len(observed) != len(required):
        raise Pending("missing operator phase(s): " + ", ".join(required[len(observed):]))
    return {event: value for event, value in events if event in required}


def receipt(session: Path, name: str, generation: str, action: str):
    output = load_small(session / (name + ".out"), 4096).strip()
    status = gate.load(session / (name + ".status.json"))
    gate.need(set(status) == {"requested", "previous", "note"}, "unexpected mediactl status projection")
    gate.need(status["requested"] == generation, "mediactl requested generation mismatch")
    if action == "install":
        version = "demo-a" if name == "install-a" else "demo-b"
        wanted = f"MEDIA_INSTALLED version={version} requested_generation={generation} ui_application=deferred-until-safe-point"
    else:
        wanted = f"MEDIA_ROLLBACK requested_generation={generation}"
    gate.need(output == wanted, "missing/ambiguous signed installer response")


def requested_store(session: Path, expected: str):
    raw = load_small(session / "store/current", 130)
    lines = raw.splitlines()
    gate.need(len(lines) == 2 and lines[0] == expected and gate.digest(lines[1]),
              "wrong final media-store version")


def media_gate(events: list[tuple[str, str]], session: Path, run: Path, report: dict) -> dict:
    stages = stage_sequence(events, MEDIA_SEQUENCE)
    first, second, restored = (stages[name] for name in GENERATION_ORDER)
    gate.need(first != second and first == restored, "A/B or rollback identity invalid")
    for name, generation, action in (("install-a", first, "install"), ("install-b", second, "install"),
                                     ("rollback", restored, "rollback")):
        receipt(session, name, generation, action)
    requested_store(session, restored)
    raw = load_small(run / "startup.log", 16 * 1024 * 1024)
    applied = re.findall(r"^MEDIA_APPLIED generation=([0-9a-f]{64})$", raw, re.MULTILINE)
    gate.need(len(applied) == report.get("media_applied"), "MEDIA_APPLIED / report counter conflict")
    index = 0
    for item in applied:
        if item == (first, second, restored)[index]:
            index += 1
            if index == 3:
                break
    gate.need(index == 3, "requested A-B-A not applied in same runtime")
    gate.need(gate.integer(report.get("media_deferred"), 1), "no runtime media busy deferral")
    return {"state": "consistent", "requested_generations": [first, second, restored],
            "applied_count": report["media_applied"], "deferred_count": report["media_deferred"],
            "human_video_review_required": True}


GENERATION_ORDER = ("A_REQUESTED", "B_BUSY_REQUESTED", "ROLLBACK_BUSY_REQUESTED")


def fault_gate(events: list[tuple[str, str]], report: dict) -> dict:
    stage_sequence(events, FAULT_SEQUENCE)
    for key in ("disconnects", "reconnects", "syn_dropped"):
        gate.need(gate.integer(report.get(key), 1), "no real runtime fault recovery counter: " + key)
    return {"state": "consistent", "disconnects": report["disconnects"],
            "reconnects": report["reconnects"], "syn_dropped": report["syn_dropped"],
            "human_video_review_required": True, "approved_external_sop_not_automatically_verified": True}


def check(action) -> dict:
    try:
        return action()
    except Pending as exc:
        return {"state": "pending", "reason": str(exc)}
    except ERRORS as exc:
        return {"state": "rejected", "reason": str(exc)}


def inspect(bundle: Path, session: Path, run: Path) -> dict:
    for path in (bundle, session, run):
        gate.no_symlinks(path)
    manifest = gate.approved_bundle(bundle / "manifest.json")
    identity = provenance(bundle, session, manifest)
    profile = identity["profile"]
    events = actions(session)
    result = {"schema": 1, "scope": "read-only-partial-media-fault-review",
              "source_commit": manifest["source_commit"], "source_tree": manifest["source_tree"],
              "profile": profile, "board_accepted": False, "product_admitted": False,
              "human_review_required": True, "runtime": {}, "media": {}, "fault": {}}

    def runtime():
        if not run.is_dir():
            raise Pending("missing original Coffee LOG_DIR")
        required = ("startup.log", "runtime/report.json", "runtime/display.json",
                    "runtime/input.json", "runtime/timeline.csv", "runtime/first.ppm", "runtime/last.ppm")
        if any(not (run / name).is_file() for name in (*required, "SHA256SUMS")):
            raise Pending("original Coffee run is missing SHA256SUMS or required files")
        gate.verify_run_sums(run, required)
        report = gate.load(run / "runtime/report.json")
        gate.common_runtime_gate(report, profile, manifest)
        gate.startup_gate(run / "startup.log", profile, "coffee", manifest)
        gate.display_gate(gate.load(run / "runtime/display.json"), profile)
        gate.input_device_gate(gate.load(run / "runtime/input.json"), profile)
        gate.timeline_gate(run / "runtime/timeline.csv", report, False)
        for name in ("first.ppm", "last.ppm"):
            gate.ppm_gate(run / "runtime" / name, profile)
        return {"state": "consistent", "report_sha256": gate.sha(run / "runtime/report.json"),
                "report": report}

    checked = check(runtime)
    result["runtime"] = {k: v for k, v in checked.items() if k != "report"}
    report = checked.get("report")
    if report is None:
        result["media"] = {"state": "pending", "reason": "original Coffee runtime not verified"}
        result["fault"] = {"state": "pending", "reason": "original Coffee runtime not verified"}
    else:
        result["media"] = check(lambda: media_gate(events, session, run, report))
        result["fault"] = check(lambda: fault_gate(events, report))
    states = {result[k]["state"] for k in ("runtime", "media", "fault")}
    result["state"] = "rejected" if "rejected" in states else "pending" if "pending" in states else "consistent"
    result["exit_code"] = {"rejected": 1, "pending": 2, "consistent": 0}[result["state"]]
    # Consistency is not a reviewer signature, cannot attest device/video authenticity.
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--output", type=Path, help="Create new JSON outside all original inputs")
    args = parser.parse_args(argv)
    try:
        if args.output:
            gate.no_symlinks(args.output)
            target = args.output.resolve()
            for protected in (args.bundle.resolve(), args.session.resolve(), args.run.resolve()):
                gate.need(not target.is_relative_to(protected), "output inside original evidence/bundle")
            gate.need(not args.output.exists(), "output exists; do not overwrite evidence")
        result = inspect(args.bundle, args.session, args.run)
        text = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
        if args.output:
            with args.output.open("x", encoding="utf-8") as dst:
                dst.write(text)
        else:
            print(text, end="")
        return result["exit_code"]
    except (*ERRORS, Pending) as exc:
        print("R6_MEDIA_FAULT_REVIEW_REJECTED: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
