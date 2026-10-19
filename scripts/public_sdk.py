#!/usr/bin/env python3
"""F7-01 experimental Public UI SDK verification and development .pui packaging.

Reuses the existing exact R3 builder and REAL native/ARM ui-framework runtimes.
Neither this checker nor a SHA256 manifest authenticates publisher identity.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pui

ROOT = Path(__file__).resolve().parents[1]
FACADE = ROOT / "sdk/ui/js/pocket-ui.js"
TYPES = ROOT / "sdk/ui/types/index.d.ts"
CONTRACT = ROOT / "sdk/ui/public-contract.json"
APP = ROOT / "apps/sdk-demo/application.js"
MANIFEST = ROOT / "apps/sdk-demo/manifest.json"
NATIVE_HEADER = ROOT / "hosts/linux/package/package.h"
TARGETS = ("imx6ul-1024x600", "imx6ul-1024x800")


class PublicSDKError(ValueError):
    pass


def reject(condition, message):
    if not condition:
        raise PublicSDKError(message)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def pairs(rows):
    result = {}
    for key, value in rows:
        reject(key not in result, "duplicate configuration key: " + key)
        result[key] = value
    return result


def read_json(path):
    reject(path.is_file() and not path.is_symlink(), "missing/non-regular file: " + str(path))
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs)


def check() -> dict:
    contract = read_json(CONTRACT)
    manifest = read_json(MANIFEST)
    reject(contract.get("schema") == 1 and contract.get("name") == "pocket.ui"
           and contract.get("status") == "experimental-software-only", "public contract version")
    reject(contract.get("sdk_facade_version") == "0.1-experimental"
           and contract.get("production_admission") is False
           and contract.get("physical_hardware") is False, "unsupported public SDK claim")
    reject(contract.get("runtime_grants") == ["ui.core"]
           and contract.get("targets") == list(TARGETS), "unexpected public capability/targets")
    loader = contract.get("loader")
    reject(type(loader) is dict and loader.get("format_version") == 1
           and loader.get("runtime_api") == 1 and loader.get("sdk_api") == 1, "public/native loader version mismatch")
    header = NATIVE_HEADER.read_text(encoding="utf-8")
    for name in ("PUI_FORMAT_VERSION", "PUI_RUNTIME_API", "PUI_SDK_API"):
        reject(re.search(r"^#define\s+" + name + r"\s+1u\s*$", header, re.M) is not None,
               "native header diverged: " + name)
    reject(pui.CAPABILITIES["ui.core"] == 1, "public/native core capability mismatch")
    try:
        pui.validate_manifest(manifest)
    except (pui.PackageError, OSError) as exc:
        raise PublicSDKError("invalid existing R3 build manifest: " + str(exc)) from exc
    reject(manifest.get("format_version") == loader["format_version"]
           and manifest["runtime_api"] == {"min": 1, "max": 1}
           and manifest["sdk_api"] == loader["sdk_api"]
           and manifest.get("targets") == list(TARGETS)
           and manifest.get("capabilities") == ["ui.core"]
           and manifest.get("authentication") == "none"
           and manifest["budgets"]["heap_bytes"] == 2097152, "manifest exceeds F7-01 public contract")
    sdk = FACADE.read_text(encoding="utf-8")
    application = APP.read_text(encoding="utf-8")
    declaration = TYPES.read_text(encoding="utf-8")
    reject(sdk.count('global.__pocketCall') == 2, "SDK must own single native bridge binding/check")
    reject('global.PocketApplication = {' in sdk and 'global, "PocketUI"' in sdk,
           "SDK does not install actual Application VM contract")
    reject('POCKET_UI_HOST_REJECTED' in sdk and 'POCKET_UI_ACTION_OVERFLOW' in sdk,
           "missing fail-closed SDK boundaries")
    reject(application.count("PocketUI.define(") == 1, "app must declare exactly one public entry")
    forbidden = (
        r"\b__pocketCall\b", r"\bPocketApplication\b", r"\blv_[A-Za-z0-9_]+\b",
        r"\bJSValue\b", r"\bfbdev\b", r"\b/dev/(?:fb|input)\b",
        r"\b(?:require|eval)\s*\(", r"\bFunction\s*\(",
        r"(?:hosts/linux|scene_guest|pocket_runtime\.c)"
    )
    for token in forbidden:
        reject(re.search(token, application) is None, "application crosses private API: " + token)
    reject("interface PocketUIContext" in declaration and "interface PocketApp" in declaration,
           "missing matching public type declarations")
    bundle = sdk + "\n" + application + "\n"
    reject("\x00" not in bundle and 0 < len(bundle.encode("utf-8")) <= pui.SOURCE_LIMIT,
           "bundled application exceeds runtime source budget")
    return {
        "scope": "F7-01-experimental-public-facade",
        "sdk_version": contract["sdk_facade_version"],
        "runtime_api": 1, "sdk_api": 1, "format_version": 1,
        "targets": list(TARGETS), "capabilities": ["ui.core"],
        "source": {str(p.relative_to(ROOT)): digest(p)
                   for p in (FACADE, TYPES, CONTRACT, APP, MANIFEST)},
        "authenticated": False, "physical_hardware": False, "production_admission": False,
    }


def assemble(assets_dir: Path):
    check()
    reject(assets_dir.is_dir() and not assets_dir.is_symlink(), "assets directory not admitted")
    source_names = {p.name for p in assets_dir.iterdir()}
    allowed = set(pui.FILES)
    reject(pui.REQUIRED <= source_names <= allowed and "application.js" in source_names,
           "assets must be a bounded, prepared existing Framework application")
    return source_names


def packaged(assets_dir: Path) -> bytes:
    names = assemble(assets_dir)
    with tempfile.TemporaryDirectory(prefix="pocket-public-sdk-") as temp:
        tmp = Path(temp)
        for name in names:
            original = assets_dir / name
            reject(original.is_file() and not original.is_symlink()
                   and original.stat().st_size <= pui.MAX_BYTES, "untrusted asset " + name)
            if name == "application.js":
                content = FACADE.read_text() + "\n" + APP.read_text() + "\n"
                (tmp / name).write_text(content, encoding="utf-8")
            else:
                shutil.copyfile(original, tmp / name)
        result = pui.build(read_json(MANIFEST), tmp)
        for target in TARGETS:
            verification = pui.verify(result, target, allow_unsigned=True)
            reject(verification["authenticated"] is False
                   and verification["physical_hardware"] is False
                   and verification["capabilities"] == 1, "package admission scope mismatch")
        return result


def save_package(assets: Path, output: Path) -> dict:
    payload = packaged(assets)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(payload)
    return {"bytes": len(payload), "sha256": digest(output), "authenticated": False,
            "physical_hardware": False, "targets": list(TARGETS)}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def invoke(cmd, log, expected=0):
    result = subprocess.run([str(x) for x in cmd], cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=180)
    log.write_text(result.stdout, encoding="utf-8")
    reject(result.returncode == expected,
           "runtime exit " + str(result.returncode) + " expected " + str(expected)
           + ", inspect " + str(log))
    return result.stdout


def integration(native: Path, arm: Path, assets: Path, destination: Path) -> dict:
    check()
    reject(not destination.exists(), "output already exists; test evidence must not be overwritten")
    for executable in (native, arm):
        reject(executable.is_file() and not executable.is_symlink(), "missing production ELF")
    destination.mkdir(parents=True, exist_ok=False)
    package = destination / "sdk-example.pui"
    package_report = save_package(assets, package)
    result = {"schema": 1, "scope": "same-production-runtime-real-application",
              "source_commit": git("rev-parse", "HEAD"),
              "source_tree": git("rev-parse", "HEAD^{tree}"),
              "sdk_source": check()["source"], "package": package_report,
              "executables": {}, "runs": {},
              "physical_hardware": False, "production_admission": False}
    for mode, executable in (("native", native), ("arm", arm)):
        result["executables"][mode] = digest(executable)
        launcher = [] if mode == "native" else ["qemu-arm", "-cpu", "cortex-a7", "-L", "/usr/arm-linux-gnueabihf"]
        for target in TARGETS:
            name = mode + "-" + target.rsplit("x", 1)[1]
            output = destination / name
            log = destination / (name + ".log")
            invoke([*launcher, executable, "--headless", "--profile", target,
                    "--package", package, "--allow-unsigned-package", "--seconds", "1",
                    "--output", output], log)
            report = read_json(output / "report.json")
            reject(report.get("commit") == result["source_commit"] and report.get("profile") == target
                   and report.get("ok") is True and report.get("error") is None
                   and report.get("package_admitted") is True
                   and report.get("package_authenticated") is False
                   and report.get("package_sha256") == package_report["sha256"]
                   and report.get("application_capabilities") == 1
                   and report.get("application_heap_limit") == 2097152
                   and report.get("physical_io") is False
                   and report.get("visual_validated") is False
                   and report.get("business_commands") is False
                   and report.get("core_live_bytes_after_close") == 0
                   and report.get("presents", 0) >= 1,
                   "real production SDK demo failed: " + name)
            result["runs"][name] = {
                "report": digest(output / "report.json"),
                "first": digest(output / "first.ppm"),
                "last": digest(output / "last.ppm"),
                "log": digest(log), "page": report.get("application_page", 0),
                "presents": report["presents"], "package_admitted": True
            }
    # Same layout/target must rasterize byte-identically in native and ARM paths.
    for suffix in ("600", "800"):
        for key in ("first", "last"):
            reject(result["runs"]["native-" + suffix][key] ==
                   result["runs"]["arm-" + suffix][key],
                   "native/ARM real rendered pixels diverged for " + suffix + "/" + key)
    (destination / "evidence.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check")
    pack = commands.add_parser("pack")
    pack.add_argument("--assets", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True)
    acceptance = commands.add_parser("integration")
    acceptance.add_argument("--assets", type=Path, required=True)
    acceptance.add_argument("--native", type=Path, required=True)
    acceptance.add_argument("--arm", type=Path, required=True)
    acceptance.add_argument("--output", type=Path, required=True)
    opts = parser.parse_args()
    if opts.command == "check":
        result = check()
    elif opts.command == "pack":
        result = save_package(opts.assets, opts.output)
    else:
        result = integration(opts.native, opts.arm, opts.assets, opts.output)
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (PublicSDKError, OSError, ValueError, subprocess.SubprocessError,
            pui.PackageError) as exc:
        print("PUBLIC_SDK_FAILED:", str(exc), file=sys.stderr)
        sys.exit(1)
