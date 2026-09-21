#!/usr/bin/env python3
"""Fail-closed ARMv7 toolchain build and evidence checks; Python stdlib only."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out"
LOCK = ROOT / "toolchains/sources.lock.json"
TARGET = ROOT / "targets/imx6ul.json"
SOURCES = ["quickjs.c", "cutils.c", "dtoa.c", "libregexp.c", "libunicode.c"]
TESTS = {"arithmetic", "native-callback", "json-unicode", "typed-array", "promise-jobs",
         "exception", "syntax-error", "interrupt", "memory-limit", "lifecycle-100"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_lock(lock: dict) -> None:
    require(lock.get("schema_version") == 1, "unsupported source lock schema")
    for name, repo in [("pocketjs", "pocketjs"), ("quickjs", "quickjs-rs")]:
        item = lock[name]
        require(item["repository"] == f"https://github.com/pocket-stack/{repo}.git", "unexpected source repository")
        require(bool(re.fullmatch(r"[0-9a-f]{40}", item["revision"])), "source revision must be a full commit SHA")
    require(lock["quickjs"]["source_dir"] == "libquickjs-sys/embed/quickjs", "unexpected QuickJS source directory")
    require(lock["quickjs"]["sources"] == SOURCES, "unexpected QuickJS source list")
    require(bool(re.fullmatch(r"\d{4}-\d{2}-\d{2}", lock["quickjs"]["version"])), "invalid QuickJS VERSION")
    tool = lock["toolchain"]
    require(tool["rust_target"] == "armv7-unknown-linux-gnueabihf", "only the GNU ARMv7 hard-float baseline is implemented")
    require(tool["c_prefix"] == "arm-linux-gnueabihf-", "unexpected C ABI")
    require(tool["gcc_major"] == 11, "baseline requires GCC 11; use a reviewed profile for another SDK")
    require(tool["qemu_cpu"] == "cortex-a7", "wrong QEMU CPU")
    require(tool["qemu_sysroot"] == "/usr/arm-linux-gnueabihf", "unexpected QEMU sysroot")
    require(bool(re.fullmatch(r"\d+\.\d+\.\d+", tool["rust"])), "pin an exact Rust version")
    require("-mfloat-abi=hard" in tool["c_flags"], "missing hard-float flag")
    require("-mcpu=cortex-a7" in tool["c_flags"], "missing Cortex-A7 C target")
    require(tool["rust_flags"] == ["-C", "target-cpu=cortex-a7"], "unexpected Rust CPU flags")


def validate_target(target: dict) -> None:
    require(target.get("schema_version") == 1, "unsupported target schema")
    require(target["soc"] == "i.MX6UL" and target["architecture"] == "armv7", "wrong SoC/architecture")
    require(target["float_abi"] == "hard", "wrong target float ABI")
    profiles = target["profiles"]
    expected = {("imx6ul-1024x600", 1024, 600), ("imx6ul-1024x800", 1024, 800)}
    actual = {(p["id"], p["width"], p["height"]) for p in profiles}
    require(len(profiles) == 2 and actual == expected, "both confirmed display profiles are required")
    require(target["rendering"]["hardware_verified"] is False, "P0 cannot certify hardware")
    require(target["rendering"]["presentation_fps_target"] == 30, "unexpected proposed FPS target")
    require(all(value is None for value in target["device_evidence"].values()), "P0 device evidence must remain unknown")


def configs() -> dict:
    lock = read_json(LOCK)
    validate_lock(lock)
    validate_target(read_json(TARGET))
    rust_file = (ROOT / "rust-toolchain.toml").read_text()
    require(f'channel = "{lock["toolchain"]["rust"]}"' in rust_file, "Rust pins disagree")
    return lock


def executable(name: str) -> str:
    result = shutil.which(name)
    require(result is not None, f"missing required executable: {name}")
    return str(result)


def run(args: list[str], *, log: str, cwd: Path = ROOT, env: dict | None = None,
        timeout: int = 300, accept_failure: bool = False) -> subprocess.CompletedProcess:
    OUT.mkdir(exist_ok=True)
    with (OUT / log).open("a", encoding="utf-8") as stream:
        stream.write("$ " + shlex.join([str(a) for a in args]) + "\n")
        stream.flush()
        try:
            result = subprocess.run(args, cwd=cwd, env=env, timeout=timeout,
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        except (OSError, subprocess.TimeoutExpired) as error:
            stream.write(f"EXECUTION_FAILED {error}\n")
            raise RuntimeError(f"command could not finish: {args[0]}: {error}") from error
        stream.write(result.stdout)
        stream.write(f"\nexit_code={result.returncode}\n")
    print(result.stdout, end="", flush=True)
    require(accept_failure or result.returncode == 0, f"command failed ({result.returncode}); see out/{log}")
    return result


def git(*args: str, cwd: Path = ROOT) -> str:
    return run([executable("git"), *args], log="git.log", cwd=cwd).stdout.strip()


def check_checkout(directory: Path, revision: str) -> None:
    require(git("rev-parse", "HEAD", cwd=directory) == revision, "cached source commit mismatch")
    require(not git("status", "--porcelain", "--untracked-files=all", cwd=directory), "cached sources are modified; use a new clean checkout")


def source_root(lock: dict) -> Path:
    return ROOT / ".cache" / ("quickjs-" + lock["quickjs"]["revision"])


def checked_sources(lock: dict) -> Path:
    checkout = source_root(lock)
    require(checkout.is_dir(), "missing pinned sources; run make fetch")
    check_checkout(checkout, lock["quickjs"]["revision"])
    source = checkout / lock["quickjs"]["source_dir"]
    require((source / "VERSION").read_text().strip() == lock["quickjs"]["version"], "QuickJS VERSION mismatch")
    for filename in SOURCES + ["quickjs.h", "LICENSE"]:
        require((source / filename).is_file(), f"missing pinned source: {filename}")
    return source


def fetch(lock: dict) -> None:
    destination = source_root(lock)
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="source-", dir=destination.parent) as temporary:
            work = Path(temporary) / "checkout"
            work.mkdir()
            git("init", "--quiet", cwd=work)
            git("remote", "add", "origin", lock["quickjs"]["repository"], cwd=work)
            git("fetch", "--depth", "1", "origin", lock["quickjs"]["revision"], cwd=work)
            git("checkout", "--detach", "FETCH_HEAD", cwd=work)
            check_checkout(work, lock["quickjs"]["revision"])
            work.rename(destination)
    checked_sources(lock)
    print("Pinned QuickJS source integrity: PASS")


def elf_header(data: bytes) -> dict:
    require(len(data) >= 52 and data[:4] == b"\x7fELF", "missing ELF header")
    require(data[4] == 1 and data[5] == 1 and data[6] == 1, "expected little-endian ELF32")
    machine = struct.unpack_from("<H", data, 18)[0]
    flags = struct.unpack_from("<I", data, 36)[0]
    require(machine == 40, "ELF is not ARM")
    require(flags & 0x400 != 0 and flags & 0x200 == 0, "ELF is not hard-float")
    require(flags >> 24 == 5, "expected ARM EABI5")
    return {"class": "ELF32", "endianness": "little", "machine": machine,
            "float_abi": "hard", "eabi": 5, "flags": hex(flags)}


def smoke_result(output: str, returncode: int) -> None:
    require(returncode == 0, "smoke executable failed")
    passed = re.findall(r"^PASS ([a-z0-9-]+)$", output, re.MULTILINE)
    require(len(passed) == len(TESTS) and set(passed) == TESTS, "missing or duplicated smoke cases")
    require(bool(re.search(r"^SMOKE_OK tests=10 pointer_bits=32 jsvalue_bytes=(8|16)$", output, re.MULTILINE)), "missing ARM smoke summary")
    require("SMOKE_FAILED" not in output, "contradictory smoke result")


def tracked_hashes() -> dict[str, str]:
    names = git("ls-files", "-z").split("\0")
    return {name: digest(ROOT / name) for name in names if name}


def build(lock: dict) -> None:
    source = checked_sources(lock)
    require(not git("status", "--porcelain", "--untracked-files=all"), "build requires a clean committed project")
    for marker in ["build.json", "smoke.json", "verification.json", "SHA256SUMS"]:
        (OUT / marker).unlink(missing_ok=True)
    tool = lock["toolchain"]
    cc, ar = executable(tool["c_prefix"] + "gcc"), executable(tool["c_prefix"] + "ar")
    readelf = executable(tool["c_prefix"] + "readelf")
    rust_version = run([executable("rustc"), "--version"], log="build.log").stdout.strip()
    require(rust_version.split()[1] == tool["rust"], "wrong Rust version")
    gcc_version = run([cc, "-dumpfullversion"], log="build.log").stdout.strip()
    require(int(gcc_version.split(".")[0]) == tool["gcc_major"], "wrong GCC major version")
    require(run([cc, "-dumpmachine"], log="build.log").stdout.strip() == "arm-linux-gnueabihf", "wrong C target")
    objects = OUT / "objects"
    objects.mkdir(parents=True, exist_ok=True)
    compiled = []
    for path in [*(source / name for name in SOURCES), ROOT / "native/quickjs-smoke/bridge.c"]:
        output = objects / (path.stem + ".o")
        run([cc, *tool["c_flags"], '-DCONFIG_VERSION="' + lock["quickjs"]["version"] + '"',
             "-I", str(source), "-c", str(path), "-o", str(output)], log="build.log")
        compiled.append(str(output))
    archive = OUT / "libquickjs_smoke.a"
    archive.unlink(missing_ok=True)
    run([ar, "rcs", str(archive), *compiled], log="build.log")
    environment = os.environ.copy()
    environment.update({"QUICKJS_LIB_DIR": str(OUT),
                        "CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER": cc,
                        "CARGO_ENCODED_RUSTFLAGS": "\x1f".join(tool["rust_flags"])})
    run([executable("cargo"), "build", "--locked", "--release", "--target", tool["rust_target"],
         "-p", "toolchain-smoke"], env=environment, log="build.log")
    binary = OUT / "toolchain-smoke"
    shutil.copy2(ROOT / "target" / tool["rust_target"] / "release/toolchain-smoke", binary)
    abi = elf_header(binary.read_bytes())
    report = run([readelf, "-h", "-A", "-l", "-d", "-V", str(binary)], log="elf.log").stdout
    interpreter = re.search(r"Requesting program interpreter: ([^\]]+)", report)
    require(interpreter is not None and interpreter.group(1) == "/lib/ld-linux-armhf.so.3", "unexpected ELF loader")
    versions = sorted(set(re.findall(r"Name: GLIBC_([\d.]+)", report)), key=lambda s: tuple(map(int, s.split("."))))
    require(bool(versions), "missing GNU libc symbol requirements")
    abi.update({"interpreter": interpreter.group(1), "glibc_symbol_floor": versions[-1],
                "needed": re.findall(r"Shared library: \[([^\]]+)\]", report),
                "physical_board_compatible": None})
    shutil.copy2(source / "LICENSE", OUT / "quickjs-LICENSE.txt")
    package_versions = run([executable("dpkg-query"), "-W", "gcc-11-arm-linux-gnueabihf",
                            "libc6-armhf-cross", "libc6-dev-armhf-cross", "binutils-arm-linux-gnueabihf", "qemu-user"], log="packages.log").stdout
    manifest = {"schema_version": 1, "created_at": datetime.now(timezone.utc).isoformat(),
                "commit": git("rev-parse", "HEAD"), "lock_sha256": digest(LOCK), "target_sha256": digest(TARGET),
                "source_files": tracked_hashes(), "lock": lock,
                "quickjs_files": {str(p.relative_to(source)): digest(p) for p in sorted(source.glob("*")) if p.is_file()},
                "tools": {"rustc": rust_version, "gcc": gcc_version, "packages": package_versions},
                "abi": abi, "artifacts": {p.name: digest(p) for p in [binary, archive, OUT / "quickjs-LICENSE.txt"]},
                "pocketjs_core_built": False, "hardware_tested": False}
    write_json(OUT / "build.json", manifest)
    print("ARMv7 build: PASS (board ABI compatibility not established)")


def verify_build() -> dict:
    manifest = read_json(OUT / "build.json")
    require(manifest["schema_version"] == 1, "unknown build evidence schema")
    require(manifest["commit"] == git("rev-parse", "HEAD"), "stale commit evidence")
    require(manifest["lock_sha256"] == digest(LOCK) and manifest["target_sha256"] == digest(TARGET), "source/configuration lock changed")
    require(manifest["source_files"] == tracked_hashes(), "project sources changed after build")
    require(not git("status", "--porcelain", "--untracked-files=all"), "project checkout is not clean")
    expected = {"toolchain-smoke", "libquickjs_smoke.a", "quickjs-LICENSE.txt"}
    require(set(manifest["artifacts"]) == expected, "missing or unexpected artifacts")
    for name, expected_hash in manifest["artifacts"].items():
        require(digest(OUT / name) == expected_hash, f"artifact hash mismatch: {name}")
    elf_header((OUT / "toolchain-smoke").read_bytes())
    require(manifest["hardware_tested"] is False and manifest["pocketjs_core_built"] is False, "P0 scope misrepresented")
    return manifest


def smoke(lock: dict) -> None:
    verify_build()
    (OUT / "smoke.json").unlink(missing_ok=True)
    tool = lock["toolchain"]
    qemu = executable("qemu-arm")
    require((Path(tool["qemu_sysroot"]) / "lib/ld-linux-armhf.so.3").exists(), "QEMU sysroot loader missing")
    version = run([qemu, "--version"], log="smoke.log").stdout.splitlines()[0]
    command = [qemu, "-cpu", tool["qemu_cpu"], "-L", tool["qemu_sysroot"], str(OUT / "toolchain-smoke")]
    result = run(command, log="smoke.log", timeout=120, accept_failure=True)
    smoke_result(result.stdout, result.returncode)
    (OUT / "smoke.stdout").write_text(result.stdout, encoding="utf-8")
    negative = run([*command, "--self-test-failure"], log="negative-smoke.log", timeout=30, accept_failure=True)
    require(negative.returncode != 0 and "intentional-failure: expected 42, received 41" in negative.stdout,
            "negative smoke failed to detect an incorrect result")
    (OUT / "negative-smoke.stdout").write_text(negative.stdout, encoding="utf-8")
    write_json(OUT / "smoke.json", {"schema_version": 1, "mode": "qemu-user-cortex-a7", "qemu": version,
               "build_sha256": digest(OUT / "build.json"), "binary_sha256": digest(OUT / "toolchain-smoke"),
               "stdout_sha256": digest(OUT / "smoke.stdout"), "returncode": result.returncode,
               "negative_stdout_sha256": digest(OUT / "negative-smoke.stdout"), "negative_returncode": negative.returncode,
               "tests": sorted(TESTS), "hardware_tested": False})
    print("ARM/QEMU functional smoke and negative gate: PASS; no FPS measurement")


def verify(lock: dict) -> None:
    manifest = verify_build()
    checked_sources(lock)
    evidence = read_json(OUT / "smoke.json")
    require(evidence["build_sha256"] == digest(OUT / "build.json"), "smoke not bound to current build")
    require(evidence["binary_sha256"] == digest(OUT / "toolchain-smoke"), "smoke binary changed")
    require(evidence["stdout_sha256"] == digest(OUT / "smoke.stdout"), "smoke output changed")
    smoke_result((OUT / "smoke.stdout").read_text(), evidence["returncode"])
    require(evidence["negative_stdout_sha256"] == digest(OUT / "negative-smoke.stdout"), "negative output changed")
    require(evidence["negative_returncode"] != 0 and "intentional-failure: expected 42, received 41" in (OUT / "negative-smoke.stdout").read_text(), "negative gate missing")
    require(evidence["hardware_tested"] is False and evidence["mode"] == "qemu-user-cortex-a7", "invalid test attribution")
    write_json(OUT / "verification.json", {"status": "passed", "scope": "P0 cross-toolchain only",
               "commit": manifest["commit"], "build_sha256": digest(OUT / "build.json"),
               "smoke_sha256": digest(OUT / "smoke.json"), "hardware_tested": False})
    files = sorted(p for p in OUT.iterdir() if p.is_file() and p.name != "SHA256SUMS")
    (OUT / "SHA256SUMS").write_text("".join(f"{digest(p)}  {p.name}\n" for p in files), encoding="utf-8")
    print("P0 evidence: PASS; physical hardware/display/performance remain unverified")


def main() -> int:
    try:
        require(len(sys.argv) == 2 and sys.argv[1] in {"check", "fetch", "build", "smoke", "verify"},
                "usage: python3 scripts/port.py {check|fetch|build|smoke|verify}")
        action = sys.argv[1]
        # Never leave a previous PASS marker behind when a new attempt fails.
        for marker in ["verification.json", "SHA256SUMS"]:
            (OUT / marker).unlink(missing_ok=True)
        if action == "build":
            for marker in ["build.json", "smoke.json"]:
                (OUT / marker).unlink(missing_ok=True)
        if action == "smoke":
            (OUT / "smoke.json").unlink(missing_ok=True)
        lock = configs()
        if action == "check":
            run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-v"], log="check.log")
        else:
            {"fetch": fetch, "build": build, "smoke": smoke, "verify": verify}[action](lock)
        return 0
    except (RuntimeError, KeyError, ValueError, OSError) as error:
        print(f"PORT_FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
