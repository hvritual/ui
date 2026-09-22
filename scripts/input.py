#!/usr/bin/env python3
"""P3-01 evdev state/replay evidence and static trace package."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/input"
CASES = {
    "state-unchanged-axis-retained-across-lift",
    "state-invalid-resync-atomic-and-disconnect-clears",
    "state-1024x800-eight-orientation-mappings",
    "coordinate-map-round-clamp-invert-swap",
    "mtb-tap-drag-normal-release",
    "mtb-multi-slot-stable-id",
    "mtb-overflow-cancel-suppress-until-all-up",
    "syn-dropped-cancel-resync-all-up-gate",
    "disconnect-cancels-published",
    "legacy-single-touch-fallback",
    "pending-release-before-syn-dropped-still-cancels",
    "tracking-id-replacement-cancel-then-rearm",
    "invalid-config-and-slot-fail-closed",
}
SOURCES = [
    "hosts/linux/input/state.h",
    "hosts/linux/input/state.c",
    "hosts/linux/input/replay_main.c",
    "hosts/linux/input/trace_capture.c",
    "hosts/linux/input/probe_main.c",
    "tests/input/test_state.c",
    "scripts/input.py",
    "scripts/device/run-input-probe.sh",
    "scripts/device/run-input-trace.sh",
    "targets/input.json",
    "contracts/input-trace.schema.json",
    "docs/input.md",
    "docs/evidence/myimx6ek140-input-20260922.json",
]

def fail(msg: str) -> None:
    raise RuntimeError(msg)

def digest(path: Path) -> str:
    h=hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024),b""): h.update(chunk)
    return h.hexdigest()

def run(args, log: Path, expected=0, *, input_text=None, env=None, timeout=180):
    args=[str(x) for x in args]
    log.parent.mkdir(parents=True,exist_ok=True)
    result=subprocess.run(args,cwd=ROOT,input=input_text,text=True,stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,env=env,timeout=timeout)
    with log.open("a",encoding="utf-8") as f:
        f.write("$ "+shlex.join(args)+"\n")
        if input_text is not None: f.write(f"<stdin bytes={len(input_text.encode())}>\n")
        f.write(result.stdout+f"\nexit_code={result.returncode}\n")
    print(result.stdout,end="")
    if result.returncode!=expected: fail(f"exit {result.returncode} expected {expected}: {log}")
    return result.stdout

def git(*args):
    return subprocess.check_output(["git",*args],cwd=ROOT,text=True).strip()

def source_hashes():
    return {p:digest(ROOT/p) for p in SOURCES}

def project_state():
    if git("status","--porcelain","--untracked-files=all"):
        fail("committed clean project required")
    return {"commit":git("rev-parse","HEAD"),"source_files":source_hashes()}

def tools(mode):
    if mode=="native":
        return "gcc", []
    if mode=="arm":
        return "arm-linux-gnueabihf-gcc", ["qemu-arm","-cpu","cortex-a7","-L","/usr/arm-linux-gnueabihf"]
    fail("invalid mode")

def validate_state(text):
    actual=re.findall(r"^PASS ([a-z0-9-]+)$",text,re.MULTILINE)
    if len(actual)!=len(CASES) or set(actual)!=CASES: fail("missing/duplicate state cases")
    if text.count("INPUT_STATE_OK cases=13 hardware_trace_required=true")!=1:
        fail("invalid state summary")

def synthetic_trace():
    ev=[
        (3,47,0),(3,57,1),(3,53,0),(3,54,0),(0,0,0),
        (3,47,0),(3,53,8192),(3,54,8192),(0,0,0),
        (3,47,0),(3,57,-1),(0,0,0),
    ]
    return {
        "schema_version":1,
        "source":"synthetic-fixture",
        "device":{
            "name":"ilitek-fixture",
            "event_path":"/dev/input/event9",
            "capability_hash":"0"*64,
        },
        "events":[
            {"seq":i,"monotonic_ns":i*1000000,"type":t,"code":c,"value":v}
            for i,(t,c,v) in enumerate(ev)
        ],
    }

def validate_trace(doc):
    if doc.get("schema_version")!=1 or doc.get("source") not in {"synthetic-fixture","real-device-redacted"}:
        fail("invalid trace header")
    device=doc.get("device",{})
    if not re.fullmatch(r"/dev/input/event[0-9]+",device.get("event_path","")):
        fail("invalid trace path")
    if not re.fullmatch(r"[0-9a-f]{64}",device.get("capability_hash","")):
        fail("invalid capability hash")
    events=doc.get("events")
    if not isinstance(events,list) or len(events)>100000: fail("invalid event array")
    last=-1
    for i,e in enumerate(events):
        if set(e)!={"seq","monotonic_ns","type","code","value"} or e["seq"]!=i:
            fail("trace event schema/sequence mismatch")
        if e["monotonic_ns"]<last: fail("non-monotonic trace")
        last=e["monotonic_ns"]

def replay_stdin(doc):
    validate_trace(doc)
    return "".join(f'{e["type"]} {e["code"]} {e["value"]}\n' for e in doc["events"])

def build_tests(mode):
    cc,runner=tools(mode)
    directory=OUT/mode
    if directory.exists(): shutil.rmtree(directory)
    directory.mkdir(parents=True)
    cflags=["-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2","-Ihosts/linux"]
    if mode=="arm": cflags += ["-mcpu=cortex-a7","-mfpu=neon-vfpv4","-mfloat-abi=hard"]

    state_bin=directory/"input-state-test"
    run([cc,*cflags,"hosts/linux/input/state.c","tests/input/test_state.c","-o",state_bin],
        directory/"build.log")
    text=run([*runner,state_bin],directory/"state.log")
    validate_state(text)

    replay_bin=directory/"input-replay"
    run([cc,*cflags,"hosts/linux/input/state.c","hosts/linux/input/replay_main.c","-o",replay_bin],
        directory/"build.log")
    trace=synthetic_trace()
    trace_path=directory/"synthetic-trace.json"
    trace_path.write_text(json.dumps(trace,separators=(",",":"))+"\n")
    stdin=replay_stdin(trace)
    replay=run([*runner,replay_bin,
                "--protocol","mtb","--slots","10","--width","1024","--height","600",
                "--x-min","0","--x-max","16384","--y-min","0","--y-max","16384",
                "--swap","0","--invert-x","0","--invert-y","0"],
               directory/"replay.log",input_text=stdin)
    expected=[
        "FRAME seq=1 contacts=1 0:0:0 cancels=0 suppressed=0 dropped=0",
        "FRAME seq=2 contacts=1 0:512:300 cancels=0 suppressed=0 dropped=0",
        "FRAME seq=3 contacts=0 cancels=0 suppressed=0 dropped=0",
        "REPLAY_OK events=12",
    ]
    if replay.splitlines()!=expected: fail("synthetic replay output mismatch")

    state=project_state()
    result={**state,"mode":mode,"state_cases":sorted(CASES),
            "state_binary_sha256":digest(state_bin),"replay_binary_sha256":digest(replay_bin),
            "synthetic_trace_sha256":digest(trace_path),"hardware_trace_required":True}
    (directory/"test.json").write_text(json.dumps(result,indent=2)+"\n")

def unit():
    directory=OUT/"unit"
    if directory.exists(): shutil.rmtree(directory)
    directory.mkdir(parents=True)
    binary=directory/"input-state-sanitized"
    flags=["-D_FORTIFY_SOURCE=2","-std=c11","-Wall","-Wextra","-Werror","-Wpedantic",
           "-O1","-g","-fsanitize=address,undefined","-Ihosts/linux"]
    run(["gcc",*flags,"hosts/linux/input/state.c","tests/input/test_state.c","-o",binary],
        directory/"build.log")
    text=run([binary],directory/"state.log",env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1"})
    validate_state(text)
    trace_bin=directory/"trace-native"
    run(["gcc","-D_FORTIFY_SOURCE=2","-std=c11","-Wall","-Wextra","-Werror","-Wpedantic",
         "-O2","hosts/linux/input/trace_capture.c","-o",trace_bin],directory/"build.log")
    bad=directory/"must-not-exist.json"
    text=run([trace_bin,"--capture","/dev/null","--name","ilitek_ts","--capability-hash","0"*64,
              "--duration-ms","1000","--max-events","10","--output",bad],
             directory/"trace-negative.log",expected=1)
    if "INPUT_TRACE_OPEN_FAILED" not in text or bad.exists(): fail("trace collector failed open")

def package():
    directory=OUT/"device"
    if directory.exists(): shutil.rmtree(directory)
    directory.mkdir(parents=True)
    cc="arm-linux-gnueabihf-gcc"
    flags=["-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2",
           "-mcpu=cortex-a7","-mfpu=neon-vfpv4","-mfloat-abi=hard","-static"]
    binaries={}
    for name,source in [
        ("input-probe-imx6ul-static","hosts/linux/input/probe_main.c"),
        ("input-trace-imx6ul-static","hosts/linux/input/trace_capture.c"),
    ]:
        path=directory/name
        run([cc,*flags,source,"-o",path],directory/"build.log")
        path.chmod(0o755); binaries[name]=path
        report=run(["arm-linux-gnueabihf-readelf","-h","-A","-l","-d",path],
                   directory/f"{name}.elf.log")
        if "Class:                             ELF32" not in report or "Machine:                           ARM" not in report:
            fail("device tool is not ELF32 ARM")
        if "Tag_ABI_VFP_args: VFP registers" not in report:
            fail("device tool is not hard-float")
        if "Requesting program interpreter" in report or "NEEDED" in report:
            fail("device tool unexpectedly dynamic")

    probe=run(["qemu-arm","-cpu","cortex-a7",binaries["input-probe-imx6ul-static"],
               "--probe","/dev/null"],directory/"qemu-probe-negative.log",expected=1)
    if "INPUT_NOT_EVDEV" not in probe: fail("input probe negative mismatch")
    bad=directory/"qemu-trace-must-not-exist.json"
    trace=run(["qemu-arm","-cpu","cortex-a7",binaries["input-trace-imx6ul-static"],
               "--capture","/dev/null","--name","ilitek_ts","--capability-hash","0"*64,
               "--duration-ms","1000","--max-events","10","--output",bad],
              directory/"qemu-trace-negative.log",expected=1)
    if "INPUT_TRACE_OPEN_FAILED" not in trace or bad.exists(): fail("ARM trace collector failed open")

    for source,name in [
        ("scripts/device/run-input-probe.sh","run-input-probe.sh"),
        ("scripts/device/run-input-trace.sh","run-input-trace.sh"),
        ("targets/input.json","input.json"),
        ("contracts/input-trace.schema.json","input-trace.schema.json"),
        ("docs/input.md","README-input.md"),
        ("docs/evidence/myimx6ek140-input-20260922.json","capability-evidence.json"),
    ]:
        shutil.copy2(ROOT/source,directory/name)
    for name in ("run-input-probe.sh","run-input-trace.sh"):
        (directory/name).chmod(0o755)

    input_config=json.loads((ROOT/"targets/input.json").read_text())
    transform=input_config["profiles"][0]["transform"]
    manifest={
        "schema_version":1,
        **project_state(),
        "target":"i.MX6UL Cortex-A7 ARMv7 hard-float",
        "device_selector":"ilitek_ts + Protocol-B capabilities",
        "observed_path":"/dev/input/event1",
        "observed_path_is_identity":False,
        "protocol":"mt-protocol-b-preferred",
        "axis_range":[0,16384],
        "hardware_slots":10,
        "runtime_contact_budget":8,
        "coordinate_transform":transform["status"],
        "transform":{"swap_xy":transform["swap_xy"],"invert_x":transform["invert_x"],"invert_y":transform["invert_y"]},
        "binaries":{name:digest(path) for name,path in binaries.items()},
    }
    (directory/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    tracked=[p for p in directory.iterdir() if p.is_file() and p.name!="SHA256SUMS"]
    sums=directory/"SHA256SUMS"
    sums.write_text("".join(f"{digest(p)}  {p.name}\n" for p in sorted(tracked)))
    package_path=OUT/"imx6ul-p3-01-trace-test.tar.gz"
    package_path.unlink(missing_ok=True)
    with tarfile.open(package_path,"w:gz",format=tarfile.PAX_FORMAT) as tar:
        for p in sorted(directory.iterdir()):
            tar.add(p,arcname=Path("p3-01-input")/p.name,recursive=False)
    print(f"INPUT_DEVICE_PACKAGE_OK package={package_path} sha256={digest(package_path)}")

def verify():
    state=project_state()
    for mode in ("native","arm"):
        result=json.loads((OUT/mode/"test.json").read_text())
        if result["commit"]!=state["commit"] or result["source_files"]!=state["source_files"]:
            fail("stale input test evidence")
        if set(result["state_cases"])!=CASES or not result["hardware_trace_required"]:
            fail("wrong input test scope")
        if result["state_binary_sha256"]!=digest(OUT/mode/"input-state-test"):
            fail("state binary drift")
        if result["replay_binary_sha256"]!=digest(OUT/mode/"input-replay"):
            fail("replay binary drift")
    manifest=json.loads((OUT/"device/manifest.json").read_text())
    if manifest["commit"]!=state["commit"] or manifest["source_files"]!=state["source_files"]:
        fail("stale device package")
    input_config=json.loads((ROOT/"targets/input.json").read_text())
    transform=input_config["profiles"][0]["transform"]
    verification={"status":"passed","commit":state["commit"],"scope":"p3-01-state-trace-replay",
                  "real_capability":"ilitek_ts-protocol-b","coordinate_transform":transform["status"],
                  "transform":{"swap_xy":transform["swap_xy"],"invert_x":transform["invert_x"],"invert_y":transform["invert_y"]},
                  "pocketjs_delivery":False,
                  "tests":{m:digest(OUT/m/"test.json") for m in ("native","arm")},
                  "device_manifest_sha256":digest(OUT/"device/manifest.json")}
    (OUT/"verification.json").write_text(json.dumps(verification,indent=2)+"\n")
    entries=[OUT/"verification.json",OUT/"imx6ul-p3-01-trace-test.tar.gz"]
    for mode in ("native","arm"):
        entries += [p for p in (OUT/mode).iterdir() if p.is_file()]
    entries += [p for p in (OUT/"unit").iterdir() if p.is_file()]
    sums=OUT/"SHA256SUMS"
    sums.write_text("".join(f"{digest(p)}  {p.relative_to(OUT)}\n" for p in sorted(entries)))
    with zipfile.ZipFile(OUT/"acceptance.zip","w",zipfile.ZIP_DEFLATED) as z:
        for p in [*entries,sums]: z.write(p,str(p.relative_to(OUT)))
    print("INPUT_EVIDENCE_OK native/ARM state+replay; coordinate transform bound to targets/input.json")

def main():
    try:
        if len(sys.argv)<2: fail("input.py unit | test native|arm | package | verify")
        OUT.mkdir(parents=True,exist_ok=True)
        action=sys.argv[1]
        if action=="unit" and len(sys.argv)==2: unit()
        elif action=="test" and len(sys.argv)==3 and sys.argv[2] in {"native","arm"}: build_tests(sys.argv[2])
        elif action=="package" and len(sys.argv)==2: package()
        elif action=="verify" and len(sys.argv)==2: verify()
        else: fail("invalid input command")
        return 0
    except (RuntimeError,KeyError,ValueError,OSError,subprocess.TimeoutExpired) as e:
        print(f"INPUT_FAILED: {e}",file=sys.stderr); return 1

if __name__=="__main__": sys.exit(main())
