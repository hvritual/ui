#!/usr/bin/env python3
"""Build and verify the real pinned PocketJS Linux host. No hardware claims."""
from __future__ import annotations
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/runtime"
SPEC = importlib.util.spec_from_file_location("port", ROOT / "scripts/port.py")
port = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(port)
require, read_json, write_json, digest = port.require, port.read_json, port.write_json, port.digest
LOCK = ROOT / "toolchains/runtime.lock.json"
PROFILES = ROOT / "targets/linux-host.json"
TESTS = {"render-1024x600", "render-1024x800", "incremental-idle", "frame-stage-order",
         "promise-layout-update", "node-remove-destroy", "wide-touch-wire", "exclusive-runtime",
         "clock-60-turns-30-renders", "pause-resume", "bounded-catchup", "backwards-clock",
         "guest-error-cleanup", "unknown-profile", "syntax-error-cleanup", "missing-frame-cleanup",
         "lifecycle-100-zero-core-allocations", "bounded-assets-and-paths", "allocator-alignment-overflow"}


def run(args, *, cwd=ROOT, env=None, log="commands.log", expected=0, timeout=300):
    OUT.mkdir(parents=True, exist_ok=True)
    args = list(map(str, args))
    with (OUT / log).open("a", encoding="utf-8") as stream:
        stream.write("$ " + shlex.join(args) + "\n"); stream.flush()
        result = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
        stream.write(result.stdout + f"\nexit_code={result.returncode}\n")
    print(result.stdout, end="", flush=True)
    require(result.returncode == expected, f"exit {result.returncode} expected {expected}: see out/runtime/{log}")
    return result.stdout


def git(*args, cwd=ROOT):
    return run([port.executable("git"), *args], cwd=cwd).strip()


def validate_profiles(data):
    require(data.get("schema_version") == 1 and data["host_id"] == "linux-headless" and data["host_abi"] == 1, "invalid downstream host identity")
    require(data["upstream_registered"] is False and data["hardware_access"] is False, "P1 headless is not an official platform or hardware host")
    require(data["simulation_hz"] == 60 and data["render_every_turns"] == 2, "frame contract changed")
    expected = {("imx6ul-1024x600", 1024, 600), ("imx6ul-1024x800", 1024, 800)}
    require(len(data["profiles"]) == 2 and {(p["id"], p["width"], p["height"]) for p in data["profiles"]} == expected, "both exact viewports required")
    profiles = {p["id"]: p for p in data["profiles"]}
    require(profiles["imx6ul-1024x600"]["board_record"] == "targets/boards/myimx6ek140-1024x600.json", "missing board provenance")
    require(profiles["imx6ul-1024x800"]["board_record"] is None, "cannot copy 600-panel observation to 800-panel")
    require(data["pixel_format"] == "BGRA8888-opaque", "wrong core pixel contract")
    require(data["text_capabilities"]["ime_composition"] == "not-admitted", "IME is not delivered by P1")


def validate_board(board):
    require(board["schema_version"] == 1 and board["profile"] == "imx6ul-1024x600", "wrong observed board")
    provenance = board["provenance"]
    require(provenance["kind"] == "user-provided-command-output" and provenance["runtime_executed_on_board"] is False, "observation is not host execution evidence")
    require(provenance["raw_file"] == "docs/evidence/myimx6ek140-user-snapshot.txt", "unexpected evidence path")
    raw = ROOT / provenance["raw_file"]
    require(digest(raw) == provenance["sha256"], "board evidence hash changed")
    text = raw.read_text()
    obs = board["observed"]
    for key, name in [("mem_total_kib", "MemTotal"), ("mem_available_snapshot_kib", "MemAvailable"), ("swap_total_kib", "SwapTotal"), ("cma_total_kib", "CmaTotal")]:
        match = re.search(rf"^{name}:\s+(\d+) kB$", text, re.MULTILINE)
        require(match is not None and obs[key] == int(match.group(1)), "memory observation disagrees with raw source")
    require(obs["kernel_release"] in text and obs["os_version"] in text, "OS observation mismatch")
    geometry = list(map(int, re.search(r"geometry (\d+ \d+ \d+ \d+ \d+)", text).group(1).split()))
    require(obs["fbset"]["geometry"] == geometry == [1024, 600, 1024, 600, 32], "observed display is 32 bpp, not a proposed RGB565 profile")
    require(all(value is None for value in board["unknown"].values()), "unmeasured hardware fields cannot be guessed")
    require(board["board_admission"] == "blocked-libc-loader-sdk-unverified", "board ABI has not been admitted")


def config():
    data = read_json(LOCK); base = port.configs(); profiles = read_json(PROFILES)
    validate_profiles(profiles)
    validate_board(read_json(ROOT / "targets/boards/myimx6ek140-1024x600.json"))
    require(data["schema_version"] == 1 and data["upstream_revision"] == base["pocketjs"]["revision"], "upstream pins disagree")
    require(data["rust"] == "nightly-2026-07-02", "runtime compiler must match pinned upstream")
    require(data["features"] == ["bare-platform", "software-only", "host-allocator"], "unexpected runtime features")
    require(set(data["targets"]) == {"native", "arm"}, "both execution targets required")
    require(data["targets"]["arm"]["triple"] == base["toolchain"]["rust_target"], "ARM ABI mismatch")
    require(data["targets"]["arm"]["runner"] == ["qemu-arm", "-cpu", "cortex-a7", "-L", "/usr/arm-linux-gnueabihf"], "wrong ARM execution attribution")
    require(len(data["patches"]) == 1, "review source patches explicitly")
    for item in data["patches"]:
        require(item["path"] == "patches/pocketjs/release-shutdown-buffers.patch", "unexpected patch path")
        require(digest(ROOT / item["path"]) == item["sha256"], "patch hash mismatch")
    return data, base, profiles


def upstream(data):
    return ROOT / ".cache" / ("pocketjs-" + data["upstream_revision"])


def source_check(data):
    src = upstream(data)
    port.check_checkout(src, data["upstream_revision"])
    for path, sha in data["upstream_files"].items():
        require(not Path(path).is_absolute() and ".." not in Path(path).parts, "invalid upstream evidence path")
        require(digest(src / path) == sha, f"upstream source mismatch: {path}")
    return src


def fetch(data, base):
    port.fetch(base)
    target = upstream(data)
    if not target.exists():
        target.parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=target.parent) as temp:
            work = Path(temp) / "checkout"; work.mkdir()
            git("init", "-q", cwd=work); git("remote", "add", "origin", base["pocketjs"]["repository"], cwd=work)
            git("fetch", "--depth", "1", "origin", data["upstream_revision"], cwd=work)
            git("checkout", "--detach", "FETCH_HEAD", cwd=work)
            port.check_checkout(work, data["upstream_revision"]); work.rename(target)
    source_check(data)


def file_hashes(directory):
    return {str(p.relative_to(directory)): digest(p) for p in sorted(directory.rglob("*")) if p.is_file()}


def project_state():
    require(not git("status", "--porcelain", "--untracked-files=all"), "committed clean project required")
    return {"commit": git("rev-parse", "HEAD"), "source_files": port.tracked_hashes()}


def prepare(data, profiles, mode):
    src = source_check(data); work = OUT / ("source-" + mode)
    if work.exists(): shutil.rmtree(work)
    work.mkdir(parents=True)
    archive = OUT / "upstream.tar"
    git("archive", "--format=tar", "-o", str(archive), "HEAD", "engine/ui-cabi", "engine/core", "engine/quickjs-c", "contracts/generated", cwd=src)
    with tarfile.open(archive) as bundle:
        for item in bundle.getmembers():
            require(not Path(item.name).is_absolute() and ".." not in Path(item.name).parts and (item.isdir() or item.isfile()), "unsafe upstream archive member")
        bundle.extractall(work)
    for patch in data["patches"]:
        run([port.executable("patch"), "--batch", "--forward", "--fuzz=0", "-p1", "-i", ROOT / patch["path"]], cwd=work)
    generated = OUT / "include"; generated.mkdir(exist_ok=True)
    entries = ",\n".join(f'    {{"{p["id"]}", {p["width"]}, {p["height"]}}}' for p in profiles["profiles"])
    (generated / "profiles.generated.h").write_text("/* Generated from targets/linux-host.json; do not edit. */\n"
        "static const struct { const char *id; unsigned width, height; } host_profiles[] = {\n" + entries + "\n};\n#define HOST_PROFILE_COUNT 2\n")
    fixtures = OUT / "fixtures"; fixtures.mkdir(exist_ok=True)
    for p in (ROOT / "tests/runtime").glob("*.js"): shutil.copy2(p, fixtures / p.name)
    (fixtures / "scene.pak").write_bytes(bytes.fromhex((ROOT / "tests/runtime/scene.pak.hex").read_text().strip()))
    return work


def build(data, base, profiles, mode):
    directory = OUT / mode; directory.mkdir(parents=True, exist_ok=True)
    for marker in ["build.json", "test.json"]: (directory / marker).unlink(missing_ok=True)
    state = project_state(); tool = data["targets"][mode]
    source = prepare(data, profiles, mode); quickjs = port.checked_sources(base)
    cc = port.executable(tool["cc"])
    tools = {"cc": run([cc, "--version"]).splitlines()[0], "rust": run(["rustc", "+" + data["rust"], "--version"]).strip()}
    require(run([cc, "-dumpfullversion"]).strip().split(".")[0] == "11", "GNU baseline requires GCC 11")
    env = os.environ.copy()
    env.pop("RUSTFLAGS", None)
    env.update({"CARGO_TARGET_DIR": str(OUT / "cargo"), "CARGO_ENCODED_RUSTFLAGS": "\x1f".join(tool["rust_flags"]),
                "CARGO_TARGET_" + tool["triple"].upper().replace("-", "_") + "_LINKER": cc})
    run(["cargo", "+" + data["rust"], "build", "--manifest-path", source / "engine/ui-cabi/Cargo.toml",
         "--locked", "--release", "--target", tool["triple"], "--features", ",".join(data["features"])], env=env, log=mode + "-build.log")
    core = OUT / "cargo" / tool["triple"] / "release/libpocketjs_symbian_core.a"
    flags = ["-std=c11", "-O2", "-fno-strict-aliasing", "-fwrapv", "-D_GNU_SOURCE", "-ffunction-sections", "-fdata-sections", *tool["c_flags"],
             '-DCONFIG_VERSION="' + base["quickjs"]["version"] + '"', '-DPOCKETJS_TARGET_ID="linux-headless"', "-DPOCKETJS_HOST_ABI=1"]
    includes = [ROOT / "hosts/linux", OUT / "include", quickjs, source / "engine/quickjs-c", source / "engine/ui-cabi/include", source / "contracts/generated"]
    for path in includes: flags += ["-I", str(path)]
    def compile_file(path, name, extra=()):
        output = directory / (name + ".o")
        run([cc, *flags, *extra, "-c", path, "-o", output], log=mode + "-build.log")
        return output
    objects = [compile_file(quickjs / name, "qjs-" + Path(name).stem) for name in base["quickjs"]["sources"]]
    archive = directory / "libquickjs.a"; archive.unlink(missing_ok=True)
    run([port.executable(tool["ar"]), "rcs", archive, *objects])
    host_objects = [compile_file(ROOT / "hosts/linux" / (name + ".c"), name, ["-Wall", "-Wextra", "-Werror"]) for name in ["host", "platform"]]
    personality = compile_file(source / "engine/quickjs-c/rust_eh_personality.c", "personality")
    for test in [False, True]:
        suffix = "test" if test else "host"
        switches = ["-DPOCKET_RUNTIME_HARNESS", "-DPOCKET_RUNTIME_STAGE_HOOKS"] if test else []
        runtime = compile_file(source / "engine/quickjs-c/pocket_runtime.c", "runtime-" + suffix, switches)
        main = compile_file(ROOT / ("tests/runtime/test_host.c" if test else "hosts/linux/main.c"), "main-" + suffix,
                            [*switches, "-Wall", "-Wextra", "-Werror"])
        binary = directory / ("runtime-test" if test else "ui-host")
        run([cc, *tool["c_flags"], "-o", binary, *host_objects, main, runtime, personality,
             archive, core, "-Wl,--gc-sections", "-lm", "-ldl", "-lpthread", "-lrt"], log=mode + "-build.log")
    report = run([port.executable(tool["readelf"]), "-h", "-A", "-l", "-d", "-V", directory / "ui-host"], log=mode + "-elf.log")
    abi = port.elf_header((directory / "ui-host").read_bytes()) if mode == "arm" else {"machine": "x86_64"}
    versions = sorted(set(re.findall(r"Name: GLIBC_([\d.]+)", report)), key=lambda s: tuple(map(int, s.split("."))))
    abi.update({"glibc_symbol_floor": versions[-1] if versions else None,
                "needed": re.findall(r"Shared library: \[([^\]]+)\]", report), "physical_board_compatible": None})
    if mode == "arm": require("/lib/ld-linux-armhf.so.3" in report, "unexpected loader")
    symbols = run([port.executable("arm-linux-gnueabihf-nm" if mode == "arm" else "nm"), directory / "ui-host"], log=mode + "-symbols.log")
    for symbol in ["JS_Eval", "ui_render_incremental", "pocket_runtime_tick"]: require(symbol in symbols, "missing real runtime symbol")
    require("pocket_bench_stage" not in symbols and "pocket_runtime_harness_" not in symbols, "test hooks leaked into host")
    artifacts = {name: digest(directory / name) for name in ["ui-host", "runtime-test", "libquickjs.a"]}
    write_json(directory / "build.json", {**state, "mode": mode, "runtime_lock_sha256": digest(LOCK), "profile_sha256": digest(PROFILES),
               "prepared_sources": file_hashes(source), "quickjs_files": file_hashes(quickjs), "fixtures": file_hashes(OUT / "fixtures"),
               "core_archive_sha256": digest(core), "generated_header_sha256": digest(OUT / "include/profiles.generated.h"), "tools": tools, "abi": abi, "artifacts": artifacts,
               "pocketjs_core_built": True, "hardware_tested": False})


def check_build(data, mode):
    build = read_json(OUT / mode / "build.json")
    state = project_state()
    require(build["commit"] == state["commit"] and build["source_files"] == state["source_files"], "stale build/project evidence")
    require(build["runtime_lock_sha256"] == digest(LOCK) and build["profile_sha256"] == digest(PROFILES), "configuration changed after build")
    require(build["mode"] == mode and build["pocketjs_core_built"] is True and build["hardware_tested"] is False, "wrong build scope")
    require(build["prepared_sources"] == file_hashes(OUT / ("source-" + mode)), "compiled upstream source changed")
    require(build["fixtures"] == file_hashes(OUT / "fixtures"), "runtime fixture changed")
    require(set(build["artifacts"]) == {"ui-host", "runtime-test", "libquickjs.a"}, "artifact set incomplete")
    for name, sha in build["artifacts"].items(): require(digest(OUT / mode / name) == sha, "artifact hash mismatch")
    require(build["generated_header_sha256"] == digest(OUT / "include/profiles.generated.h"), "generated host profile changed")
    core = OUT / "cargo" / data["targets"][mode]["triple"] / "release/libpocketjs_symbian_core.a"
    require(build["core_archive_sha256"] == digest(core), "core archive changed")
    require(build["quickjs_files"] == file_hashes(port.checked_sources(port.configs())), "QuickJS sources changed")
    source_check(data)
    return build


def validate_test(output, mode):
    names = re.findall(r"^PASS ([a-z0-9-]+)$", output, re.MULTILINE)
    require(len(names) == len(TESTS) and set(names) == TESTS, "missing or duplicate runtime cases")
    bits = 32 if mode == "arm" else 64
    require(output.count(f"RUNTIME_OK pointer_bits={bits} hardware_tested=false") == 1 and "TEST_FAILED" not in output, "invalid runtime summary")


def test(data, mode):
    build = check_build(data, mode); directory = OUT / mode; runner = data["targets"][mode]["runner"]
    for binary in runner[:1]: port.executable(binary)
    if mode == "arm": run([runner[0], "--version"], log=mode + "-test.log")
    output = run([*runner, directory / "runtime-test", OUT / "fixtures"], log=mode + "-test.log", timeout=180)
    validate_test(output, mode); (directory / "test.stdout").write_text(output)
    negative = run([*runner, directory / "runtime-test", OUT / "fixtures", "--intentional-failure"], expected=1, log=mode + "-negative.log")
    require("TEST_FAILED" in negative and "RUNTIME_OK" not in negative, "negative test did not reject wrong pixel")
    (directory / "negative.stdout").write_text(negative)
    cli_outputs = []
    for profile in ["imx6ul-1024x600", "imx6ul-1024x800"]:
        cli = run([*runner, directory / "ui-host", "--profile", profile, "--asset-root", OUT / "fixtures", "--bundle", "scene.js", "--pack", "scene.pak", "--ticks", "6"], log=mode + "-cli.log", timeout=30)
        require("HOST_OK mode=headless" in cli and "hardware_tested=false" in cli, "production host smoke missing")
        cli_outputs.append(cli)
    (directory / "cli.stdout").write_text("".join(cli_outputs))
    write_json(directory / "test.json", {"commit": build["commit"], "mode": mode, "build_sha256": digest(directory / "build.json"),
               "outputs": {n: digest(directory / n) for n in ["test.stdout", "negative.stdout", "cli.stdout"]},
               "returncode": 0, "negative_returncode": 1, "cases": sorted(TESTS), "hardware_tested": False})


def verify(data):
    commits = set()
    for mode in ["native", "arm"]:
        build = check_build(data, mode); directory = OUT / mode; result = read_json(directory / "test.json")
        require(result["commit"] == build["commit"] and result["mode"] == mode and result["build_sha256"] == digest(directory / "build.json"), "test not bound to build")
        require(result["returncode"] == 0 and result["negative_returncode"] == 1 and result["hardware_tested"] is False, "wrong execution status")
        require(set(result["outputs"]) == {"test.stdout", "negative.stdout", "cli.stdout"}, "missing execution output")
        for name, sha in result["outputs"].items(): require(digest(directory / name) == sha, "execution log changed")
        validate_test((directory / "test.stdout").read_text(), mode)
        commits.add(build["commit"])
    require(len(commits) == 1, "native and ARM evidence from different commits")
    write_json(OUT / "verification.json", {"status": "passed", "commit": commits.pop(), "scope": "real-core-linux-headless",
               "cases_per_target": len(TESTS), "hardware_tested": False, "board_abi": "unverified", "ime": "not-integrated",
               "tests": {m: digest(OUT / m / "test.json") for m in ["native", "arm"]}})
    entries = [p for p in OUT.glob("*.log")] + [OUT / "verification.json"]
    for mode in ["native", "arm"]:
        entries += [p for p in (OUT / mode).iterdir() if p.suffix in {".json", ".stdout"} or p.name in {"ui-host", "runtime-test"}]
    sums = OUT / "SHA256SUMS"
    sums.write_text("".join(f"{digest(p)}  {p.relative_to(OUT)}\n" for p in sorted(entries)))
    with zipfile.ZipFile(OUT / "acceptance.zip", "w", zipfile.ZIP_DEFLATED) as package:
        for p in [*entries, sums]: package.write(p, str(p.relative_to(OUT)))
    print("RUNTIME_EVIDENCE_OK: native + ARM; physical board and IME remain unverified")


def main():
    try:
        require(len(sys.argv) in {2, 3}, "usage: runtime.py check|fetch|build|test|verify [native|arm]")
        action = sys.argv[1]; mode = sys.argv[2] if len(sys.argv) == 3 else None
        require(action in {"check", "fetch", "build", "test", "verify"}, "unknown runtime command")
        require((mode in {"native", "arm"}) if action in {"build", "test"} else mode is None, "invalid mode")
        OUT.mkdir(parents=True, exist_ok=True)
        for marker in ["verification.json", "SHA256SUMS", "acceptance.zip"]: (OUT / marker).unlink(missing_ok=True)
        if mode:
            (OUT / mode / "test.json").unlink(missing_ok=True)
            if action == "build": (OUT / mode / "build.json").unlink(missing_ok=True)
        data, base, profiles = config()
        if action == "check": run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_runtime.py", "-v"])
        elif action == "fetch": fetch(data, base)
        elif action == "build": build(data, base, profiles, mode)
        elif action == "test": test(data, mode)
        else: verify(data)
        return 0
    except (RuntimeError, KeyError, ValueError, OSError, subprocess.TimeoutExpired) as error:
        print(f"RUNTIME_FAILED: {error}", file=sys.stderr); return 1

if __name__ == "__main__": sys.exit(main())
