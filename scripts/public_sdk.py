#!/usr/bin/env python3
"""Experimental Public UI SDK and bounded custom component .pui verification.

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
CUSTOM = ROOT / "sdk/ui/js/pocket-custom.js"
CUSTOM_TYPES = ROOT / "sdk/ui/types/custom.d.ts"
CHART = ROOT / "apps/custom-chart/bar-chart.js"
CUSTOM_APP = ROOT / "apps/custom-chart/application.js"
CUSTOM_MANIFEST = ROOT / "apps/custom-chart/manifest.json"
TREND = ROOT / "apps/trend-chart/charts.js"
TREND_APP = ROOT / "apps/trend-chart/application.js"
TREND_MANIFEST = ROOT / "apps/trend-chart/manifest.json"
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


def example_sources(example):
    reject(example in ("counter", "custom-chart", "trend-chart"), "unknown public example")
    if example == "trend-chart":
        return TREND_APP, TREND_MANIFEST, (CUSTOM, TREND)
    return ((APP, MANIFEST, ()) if example == "counter" else
            (CUSTOM_APP, CUSTOM_MANIFEST, (CUSTOM, CHART)))


def check(example="counter") -> dict:
    app_path, manifest_path, modules = example_sources(example)
    contract = read_json(CONTRACT)
    manifest = read_json(manifest_path)
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
    application = app_path.read_text(encoding="utf-8")
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
        for source in (application, *(p.read_text(encoding="utf-8") for p in modules)):
            reject(re.search(token, source) is None, "application crosses private API: " + token)
    reject("interface PocketUIContext" in declaration and "interface PocketApp" in declaration,
           "missing matching public type declarations")
    if example in ("custom-chart", "trend-chart"):
        reject(contract.get("custom_surface") == {
            "version": "0-experimental", "max_primitives": 32, "max_factories": 32,
            "primitives": ["rectangle", "axis-line"], "updates": "fixed-topology-geometry",
            "lifetime": "parent-page-owned", "trust": "trusted-development-only"
        }, "unsupported custom surface contract")
        reject("interface PocketCustomRegistry" in CUSTOM_TYPES.read_text(encoding="utf-8"),
               "missing custom surface declarations")
    if example == "trend-chart":
        reject(contract.get("drawing_protocol") == {
            "version":1,"primitive":"polyline","max_points":4096,"max_surfaces":4,
            "max_pixels_per_surface":262144,"stroke_width":1,"style":"theme-background-token",
            "resource":"PDW1","scene_wire":"PSC1-unchanged","trust":"trusted-development-only"
        }, "unsupported drawing protocol contract")
    bundle = "\n".join(p.read_text(encoding="utf-8") for p in (FACADE, *modules, app_path)) + "\n"
    reject("\x00" not in bundle and 0 < len(bundle.encode("utf-8")) <= pui.SOURCE_LIMIT,
           "bundled application exceeds runtime source budget")
    return {
        "scope": {"counter":"F7-01-experimental-public-facade", "custom-chart":"F7-02A-bounded-custom-surface",
                  "trend-chart":"F7-02B-bounded-polyline"}[example],
        "sdk_version": contract["sdk_facade_version"],
        "runtime_api": 1, "sdk_api": 1, "format_version": 1,
        "targets": list(TARGETS), "capabilities": ["ui.core"],
        "source": {str(p.relative_to(ROOT)): digest(p)
                   for p in (FACADE, TYPES, CONTRACT, app_path, manifest_path, *modules,
                             *((CUSTOM_TYPES,) if modules else ()))},
        "authenticated": False, "physical_hardware": False, "production_admission": False,
    }


def assemble(assets_dir: Path, example="counter"):
    check(example)
    reject(assets_dir.is_dir() and not assets_dir.is_symlink(), "assets directory not admitted")
    source_names = {p.name for p in assets_dir.iterdir()}
    allowed = set(pui.FILES)
    reject(pui.REQUIRED <= source_names <= allowed and "application.js" in source_names,
           "assets must be a bounded, prepared existing Framework application")
    return source_names


def packaged(assets_dir: Path, example="counter") -> bytes:
    names = assemble(assets_dir, example)
    app_path, _, modules = example_sources(example)
    with tempfile.TemporaryDirectory(prefix="pocket-public-sdk-") as temp:
        tmp = Path(temp)
        for name in names:
            original = assets_dir / name
            reject(original.is_file() and not original.is_symlink()
                   and original.stat().st_size <= pui.MAX_BYTES, "untrusted asset " + name)
            if name == "application.js":
                content = "\n".join(p.read_text(encoding="utf-8") for p in
                                    (FACADE, *modules, app_path)) + "\n"
                (tmp / name).write_text(content, encoding="utf-8")
            else:
                shutil.copyfile(original, tmp / name)
        result = pui.build(read_json(example_sources(example)[1]), tmp)
        for target in TARGETS:
            verification = pui.verify(result, target, allow_unsigned=True)
            reject(verification["authenticated"] is False
                   and verification["physical_hardware"] is False
                   and verification["capabilities"] == 1, "package admission scope mismatch")
        return result


def save_package(assets: Path, output: Path, example="counter") -> dict:
    payload = packaged(assets, example)
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


def chart_pixels(output: Path, height: int) -> dict:
    """Check actual production pixels against independently specified demo geometry.

    This does not render or replace the engine. All six bars, empty space, axes,
    and the moved surface must agree in both snapshots; blank/stale frames fail.
    """
    snapshots = []
    for filename, values, left, top, width, plot_height in (
        ("first.ppm", [20, 45, 70, 35, 85, 55], 64, 160, 896, height - 240),
        ("last.ppm", [80, 30, 50, 90, 25, 65], 96, 176, 832, height - 272),
    ):
        data = (output / filename).read_bytes()
        header = re.match(rb"P6\s+1024\s+" + str(height).encode() + rb"\s+255\r?\n", data)
        reject(header is not None, "invalid real snapshot dimensions/header")
        pixels = data[header.end():]
        reject(len(pixels) == 1024 * height * 3, "invalid real snapshot size")

        def pixel(x, y):
            at = (y * 1024 + x) * 3
            return pixels[at:at + 3]

        step = (width - 32) // 6
        for i, value in enumerate(values):
            bar_height = ((plot_height - 32) * value + 50) // 100
            x = left + 24 + i * step + (step - 4) // 2
            y = top + 8 + plot_height - 32 - bar_height
            reject(pixel(x, y + bar_height // 2) == bytes.fromhex("2a7f98"), "missing real chart bar")
            reject(pixel(x, y - 3) == bytes.fromhex("f0dfc8"), "bar exceeds expected geometry")
        reject(pixel(left + 16, top + 12) == bytes.fromhex("65564c") and
               pixel(left + width // 2, top + plot_height - 16) == bytes.fromhex("65564c"),
               "missing real axis lines")
        reject(pixel(65, 161) == bytes.fromhex("f0dfc8" if filename == "first.ppm" else "faf8f4"),
               "surface did not move/clear")
        snapshots.append(digest(output / filename))
    reject(snapshots[0] != snapshots[1], "chart geometry did not change")
    return {"bars_per_snapshot": 6, "axis_lines_per_snapshot": 2, "updated": True}


def trend_pixels(output: Path, height: int) -> dict:
    for filename,phase,left,top,width in (("first.ppm",0,64,160,896),("last.ppm",1,96,176,832)):
        data=(output/filename).read_bytes()
        header=re.match(rb"P6\s+1024\s+"+str(height).encode()+rb"\s+255\r?\n",data)
        reject(header is not None,"trend snapshot dimensions")
        pixels=data[header.end():]
        reject(len(pixels)==1024*height*3,"trend snapshot size")
        plot_height=240 if height==600 else 280

        def pixel(x,y):
            at=(y*1024+x)*3
            return pixels[at:at+3]

        for i in range(1024):
            ramp=(i*7+phase*173)%256
            value=20+(min(ramp,256-ramp)*60)//128+i%17
            x=left+24+((width-48)*i*2+1023)//(1023*2)
            y=top+plot_height-24-((plot_height-48)*value+50)//100
            reject(pixel(x,y)==bytes.fromhex("2a7f98"),"missing real trend vertex "+str(i))
        reject(pixel(left+4,top+4)==bytes.fromhex("f0dfc8"),"trend surface background")
        reject(pixel(left+16,top+12)==bytes.fromhex("65564c"),"trend vertical axis")
        reject(pixel(left+40,top+plot_height-16)==bytes.fromhex("65564c"),"trend horizontal axis")
    reject(digest(output/"first.ppm")!=digest(output/"last.ppm"),"trend did not update")
    return {"samples_per_frame":1024,"segments_per_frame":1023,"updated":True,"binned":False}


def integration(native: Path, arm: Path, assets: Path, destination: Path, example="counter",
                baseline: Path | None = None) -> dict:
    check(example)
    reject(not destination.exists(), "output already exists; test evidence must not be overwritten")
    for executable in (native, arm):
        reject(executable.is_file() and not executable.is_symlink(), "missing production ELF")
    reject(not git("status", "--porcelain", "--untracked-files=all"),
           "production integration requires a clean committed source checkout")
    asset_hashes = {name: digest(assets / name) for name in sorted(assemble(assets, example))
                    if name != "application.js"}
    baseline_report = None
    if example in ("custom-chart", "trend-chart"):
        reject(baseline is not None, "custom chart requires counter baseline evidence")
        baseline_report = read_json(baseline)
        reject(baseline_report.get("example") == "counter" and
               baseline_report.get("source_commit") == git("rev-parse", "HEAD") and
               baseline_report.get("executables") == {"native": digest(native), "arm": digest(arm)} and
               baseline_report.get("assets") == asset_hashes and
               set(baseline_report.get("runs", {})) == {"native-600", "native-800", "arm-600", "arm-800"},
               "custom chart must reuse exact counter production binaries and source commit")
    destination.mkdir(parents=True, exist_ok=False)
    package = destination / ("sdk-example.pui" if example == "counter" else example+".pui")
    package_report = save_package(assets, package, example)
    result = {"schema": 1, "scope": "same-production-runtime-real-application",
              "source_commit": git("rev-parse", "HEAD"),
              "source_tree": git("rev-parse", "HEAD^{tree}"),
              "sdk_source": check(example)["source"], "package": package_report, "example": example,
              "assets": asset_hashes,
              "test_source": {str(p.relative_to(ROOT)): digest(p) for p in
                              (Path(__file__), *sorted((ROOT / "tests/sdk").glob("test_*")))},
              "executables": {}, "runs": {},
              "physical_hardware": False, "production_admission": False}
    if baseline_report is not None:
        result["counter_baseline_sha256"] = digest(baseline)
    if example == "trend-chart":
        native_sources = ("hosts/linux/engine/drawing.h", "hosts/linux/engine/drawing_raster.h",
                          "hosts/linux/engine/scene_guest.js", "hosts/linux/engine/scene_runtime.c",
                          "hosts/linux/application/application.c", "hosts/linux/framework.c")
        result["drawing_source"] = {name:digest(ROOT/name) for name in native_sources}
    for mode, executable in (("native", native), ("arm", arm)):
        result["executables"][mode] = digest(executable)
        launcher = [] if mode == "native" else ["qemu-arm", "-cpu", "cortex-a7", "-L", "/usr/arm-linux-gnueabihf"]
        if example in ("custom-chart", "trend-chart"):
            denied_log = destination / (mode + "-unsigned-denied.log")
            denied = invoke([*launcher, executable, "--headless", "--profile", TARGETS[0],
                             "--package", package, "--output", destination / (mode + "-denied")],
                            denied_log, expected=1)
            reject("FRAMEWORK_PACKAGE_REJECTED error=PUI_UNSIGNED" in denied,
                   "production did not reject unsigned custom package")
            result.setdefault("unsigned_denials", {})[mode] = digest(denied_log)
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
            if example == "custom-chart":
                reject(report.get("application_selection") == 1, "custom chart did not update")
                result["runs"][name]["pixels"] = chart_pixels(output, int(target.rsplit("x", 1)[1]))
            if example == "trend-chart":
                reject(report.get("application_selection")==1 and report.get("drawing_points")==1024 and
                       report.get("drawing_surfaces")==1 and report.get("drawing_commits")==2 and
                       report.get("drawing_uploads")==2 and report.get("drawing_uploaded_points")==2048 and
                       report.get("nodes")==8 and report.get("item_count")==1024,
                       "real 1024-point native protocol or bounded component count failed: "+name)
                result["runs"][name]["pixels"]=trend_pixels(output,int(target.rsplit("x",1)[1]))
                result["runs"][name]["drawing_points"]=report["drawing_points"]
                result["runs"][name]["timeline"]=digest(output/"timeline.csv")
        reject(digest(executable) == result["executables"][mode], "runtime ELF changed during integration")
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
    contract_check = commands.add_parser("check")
    pack = commands.add_parser("pack")
    pack.add_argument("--assets", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True)
    acceptance = commands.add_parser("integration")
    acceptance.add_argument("--assets", type=Path, required=True)
    acceptance.add_argument("--native", type=Path, required=True)
    acceptance.add_argument("--arm", type=Path, required=True)
    acceptance.add_argument("--output", type=Path, required=True)
    acceptance.add_argument("--baseline-evidence", type=Path)
    for command in (contract_check, pack, acceptance):
        command.add_argument("--example", choices=("counter", "custom-chart", "trend-chart"), default="counter")
    opts = parser.parse_args()
    if opts.command == "check":
        result = check(opts.example)
    elif opts.command == "pack":
        result = save_package(opts.assets, opts.output, opts.example)
    else:
        result = integration(opts.native, opts.arm, opts.assets, opts.output, opts.example, opts.baseline_evidence)
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (PublicSDKError, OSError, ValueError, subprocess.SubprocessError,
            pui.PackageError) as exc:
        print("PUBLIC_SDK_FAILED:", str(exc), file=sys.stderr)
        sys.exit(1)
