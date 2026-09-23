#!/usr/bin/env python3
"""P3-02 live evdev + PocketJS contact evidence."""
from __future__ import annotations
import base64
import gzip
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
import zipfile

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"out/input-live"

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec); spec.loader.exec_module(mod); return mod

rt=load("runtime",ROOT/"scripts/runtime.py")
require,digest,read_json,write_json=rt.require,rt.digest,rt.read_json,rt.write_json
BRIDGE_CASES={
    "bridge-emergency-cancel-discards-stale-clicks",
    "bridge-unsent-down-never-becomes-click-after-cancel",
    "bridge-coordinate-wire-range-fail-closed",
    "bridge-down-move-coalesce-hit-once",
    "bridge-fast-tap-preserves-down-up",
    "bridge-cancel-terminal-once",
    "bridge-normal-release-is-absence",
    "bridge-multicontact-hit-fact-stable",
    "bridge-edge-queue-overflow-fail-closed",
}
LIVE_CASES={
    "live-open-snapshots-held-contact-until-all-up",
    "live-reconnect-rediscovers-changed-event-node",
    "live-continuous-stream-yields-to-guest-budget",
    "live-hup-without-data-is-not-readable-spin",
    "live-discovery-skips-powerkey-selects-ilitek",
    "live-drain-real-shape-tap-move-release",
    "live-syn-dropped-resync-restores-current-slot",
    "live-disconnect-emits-terminal-cancel",
    "live-poll-bounded-no-busy-loop",
}
DELIVERY_CASES={
    "delivery-focus-loss-cancels-instead-of-release-click",
    "delivery-real-trace-650-events-600",
    "delivery-real-trace-650-events-800",
    "delivery-down-hit-fact-real-core",
    "delivery-move-retains-down-hit",
    "delivery-normal-release-empty-snapshot",
    "delivery-terminal-cancel-wire",
    "delivery-fast-tap-two-guest-turns",
}
WRAPS=["open","__open_2","fstat","ioctl","read","__read_chk","poll","__poll_chk","close"]

def run(args,log,expected=0,env=None,timeout=180):
    args=list(map(str,args)); log.parent.mkdir(parents=True,exist_ok=True)
    result=subprocess.run(args,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                          text=True,env=env,timeout=timeout)
    with log.open("a",encoding="utf-8") as f:
        f.write("$ "+shlex.join(args)+"\n"+result.stdout+f"\nexit_code={result.returncode}\n")
    print(result.stdout,end="")
    require(result.returncode==expected,f"expected {expected}, got {result.returncode}: {log}")
    return result.stdout

def validate(text,cases,summary):
    actual=re.findall(r"^PASS ([a-z0-9-]+)$",text,re.MULTILINE)
    require(len(actual)==len(cases) and set(actual)==cases,"missing/duplicate input-live cases")
    require(text.count(summary)==1,"invalid input-live summary")

def compile_test(mode,name,sources,wraps=()):
    data,_,_=rt.config(); build=rt.check_build(data,mode)
    directory=OUT/mode; directory.mkdir(parents=True,exist_ok=True)
    tool=data["targets"][mode]; cc=rt.port.executable(tool["cc"]); runner=tool["runner"]
    prepared=rt.OUT/("source-"+mode)
    flags=["-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2",*tool["c_flags"],
           "-Ihosts/linux","-Itests/input","-I"+str(prepared/"engine/quickjs-c")]
    binary=directory/name
    run([cc,*flags,*sources,*["-Wl,--wrap="+w for w in wraps],"-Wl,--gc-sections","-o",binary],
        directory/"build.log")
    return data,build,runner,binary

def unit():
    data,_,_=rt.config()
    # Fetch/prepared headers are required only for pocket_runtime.h used by bridge.h.
    rt.source_check(data)
    directory=OUT/"unit"
    if directory.exists(): shutil.rmtree(directory)
    directory.mkdir(parents=True)
    prepared=rt.OUT/"source-native"
    require(prepared.is_dir(),"run native runtime build before input-live unit")
    flags=["-D_FORTIFY_SOURCE=2","-std=c11","-Wall","-Wextra","-Werror","-Wpedantic",
           "-O1","-g","-fsanitize=address,undefined","-Ihosts/linux","-Itests/input",
           "-I"+str(prepared/"engine/quickjs-c")]
    bridge=directory/"bridge-sanitized"
    run(["gcc",*flags,"hosts/linux/input/bridge.c","tests/input/test_bridge.c","-o",bridge],
        directory/"build.log")
    text=run([bridge],directory/"bridge.log",env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1"})
    validate(text,BRIDGE_CASES,"INPUT_BRIDGE_OK cases=9")
    live=directory/"live-sanitized"
    run(["gcc",*flags,"hosts/linux/input/state.c","hosts/linux/input/live.c","tests/input/test_live.c",
         *["-Wl,--wrap="+w for w in WRAPS],"-o",live],directory/"build.log")
    text=run([live],directory/"live.log",env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1"})
    validate(text,LIVE_CASES,"INPUT_LIVE_OK cases=9")

def test(mode):
    directory=OUT/mode
    if directory.exists(): shutil.rmtree(directory)
    data,build,runner,bridge=compile_test(
        mode,"bridge-test",["hosts/linux/input/bridge.c","tests/input/test_bridge.c"])
    text=run([*runner,bridge],directory/"bridge.log")
    validate(text,BRIDGE_CASES,"INPUT_BRIDGE_OK cases=9")

    _,_,runner,live=compile_test(
        mode,"live-test",["hosts/linux/input/state.c","hosts/linux/input/live.c","tests/input/test_live.c"],WRAPS)
    text=run([*runner,live],directory/"live.log")
    validate(text,LIVE_CASES,"INPUT_LIVE_OK cases=9")

    # Real PocketJS runtime/contact wire: link the harness runtime object built
    # by scripts/runtime.py instead of substituting a fake guest.
    tool=data["targets"][mode]
    cc=rt.port.executable(tool["cc"])
    prepared=rt.OUT/("source-"+mode)
    core=rt.OUT/"cargo"/tool["triple"]/"release/libpocketjs_symbian_core.a"
    delivery=directory/"delivery-test"
    delivery_flags=["-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2",*tool["c_flags"],
                    "-Ihosts/linux","-I"+str(prepared/"engine/quickjs-c")]
    delivery_objects=[
        rt.OUT/mode/"host.o",rt.OUT/mode/"platform.o",rt.OUT/mode/"input-bridge.o",rt.OUT/mode/"input-state.o",
        rt.OUT/mode/"runtime-test.o",rt.OUT/mode/"personality.o",
        rt.OUT/mode/"libquickjs.a",core,
    ]
    run([cc,*delivery_flags,"tests/input/test_delivery.c",*delivery_objects,
         "-Wl,--gc-sections","-lm","-ldl","-lpthread","-lrt","-o",delivery],
        directory/"build.log")
    fixture=read_json(ROOT/"tests/input/fixtures/ilitek-trace.json")
    raw=gzip.decompress(base64.b64decode(fixture["data"],validate=True))
    require(hashlib.sha256(raw).hexdigest()==fixture["sha256"]=="475725a67849617e2fb664a64c11d9ebfc095f463d2be4b8c06540d7d1a5f1f1", "trace byte hash mismatch")
    trace=json.loads(raw); require(len(trace["events"])==650, "trace length mismatch")
    require(trace["device"]["capability_hash"]==fixture["capability_sha256"], "trace capability mismatch")
    trace_path=directory/"real-trace.tsv"
    trace_path.write_text("".join(f'{e["type"]} {e["code"]} {e["value"]}\n' for e in trace["events"]))
    text=run([*runner,delivery,ROOT/"tests/input",trace_path],directory/"delivery.log")
    validate(text,DELIVERY_CASES,"INPUT_DELIVERY_OK cases=8 real_core=true")
    negative=run([*runner,delivery,ROOT/"tests/input",trace_path,"--intentional-failure"],directory/"delivery-negative.log",expected=1)
    require("FAIL line=" in negative and "INPUT_DELIVERY_OK" not in negative,"delivery negative bypassed")

    fixture_assets=load("touch_assets", ROOT/"tests/display/assets.py")
    scene_assets=directory/"scene-assets"; scene_assets.mkdir()
    (scene_assets/"display-font.bin").write_bytes(fixture_assets.font_bytes())
    shutil.copy2(ROOT/"tests/input/touch-scene.js",scene_assets/"touch-scene.js")
    scene_binary=directory/"touch-scene-test"
    run([cc,*delivery_flags,"tests/input/test_touch_scene.c",*delivery_objects,
         "-Wl,--gc-sections","-lm","-ldl","-lpthread","-lrt","-o",scene_binary],directory/"build.log")
    scene=run([*runner,scene_binary,scene_assets],directory/"touch-scene.log")
    validate(scene,{"touch-asymmetric-target-long-press-release-once", "touch-drag-across-targets-does-not-click",
                    "touch-terminal-cancel-never-commits-click"},"TOUCH_SCENE_OK cases=3 real_core=true")

    loop_binary=directory/"touch-loop-test"
    loop_objects=[*delivery_objects[:-2],rt.OUT/mode/"input-live.o",rt.OUT/mode/"input-cli.o",rt.OUT/mode/"media-store.o",
                  rt.OUT/mode/"display-fbdev.o",rt.OUT/mode/"display-presenter.o",*delivery_objects[-2:]]
    loop_wraps=["input_live_discover","input_live_reconnect","input_live_wait","input_live_drain",
                "input_live_close","fbdev_open","fbdev_close","fbdev_present","host_monotonic_ns","poll"]
    run([cc,*delivery_flags,"tests/input/test_touch_loop.c",*loop_objects,
         *["-Wl,--wrap="+w for w in loop_wraps],"-Wl,--gc-sections","-lm","-ldl","-lpthread","-lrt","-o",loop_binary],directory/"build.log")
    loop=run([*runner,loop_binary,scene_assets],directory/"touch-loop.log")
    validate(loop,{"touch-loop-disconnect-cancel-reconnect-real-guest", "touch-loop-no-events-cannot-pass-acceptance"},
             "TOUCH_LOOP_OK cases=2 io=fixture real_core=true")
    loop_report=read_json(scene_assets/"loop.json")
    require(loop_report["ok"] is True and loop_report["input"]["reconnects"]==1 and
            loop_report["physical_touch_validated"] is False, "loop recovery evidence mismatch")
    import csv
    with (scene_assets/"timeline.csv").open() as f:
        rows=list(csv.DictReader(f))
    require(len(rows)==loop_report["trace_rows"]>0,"missing input/guest/present timeline")
    for row in rows:
        require(int(row["guest_begin_ns"])<=int(row["guest_end_ns"]),"guest timing reversed")
        if int(row["present_end_ns"]):
            require(int(row["guest_end_ns"])<=int(row["present_begin_ns"])<=int(row["present_end_ns"]),"present timing reversed")

    production=rt.OUT/mode/"ui-host"
    nm=rt.port.executable("arm-linux-gnueabihf-nm" if mode=="arm" else "nm")
    symbols=run([nm,production],directory/"symbols.log")
    for symbol in ("input_cli","input_live_discover","input_bridge_ingest",
                   "host_turn_contacts","input_live_reconnect","input_bridge_cancel_all",
                   "pocket_runtime_tick_contacts"):
        require(symbol in symbols,f"missing production symbol {symbol}")
    require("__wrap_" not in symbols,"fixture wrapper leaked into production")

    rejected=run([*runner,production,"--touch-test","--profile","imx6ul-1024x800",
                  "--asset-root","."],directory/"cli-negative.log",expected=2)
    require("TOUCH_ARGUMENT_INVALID" in rejected,"touch CLI did not fail closed on unadmitted profile")

    state=rt.project_state()
    write_json(directory/"test.json",{**state,"mode":mode,
               "runtime_build_sha256":digest(rt.OUT/mode/"build.json"),
               "bridge_sha256":digest(bridge),"live_sha256":digest(live),
               "delivery_sha256":digest(delivery),"scene_sha256":digest(scene_binary),"loop_sha256":digest(loop_binary),
               "bridge_cases":sorted(BRIDGE_CASES),"live_cases":sorted(LIVE_CASES),
               "delivery_cases":sorted(DELIVERY_CASES),
               "production_sha256":digest(production),
               "real_trace_sha256":fixture["sha256"],
               "logs":{n:digest(directory/n) for n in ("bridge.log","live.log","delivery.log","delivery-negative.log","touch-scene.log","touch-loop.log")},
               "physical_touch_validated":False})

def verify():
    state=rt.project_state(); data,_,_=rt.config()
    for mode in ("native","arm"):
        rt.check_build(data,mode)
        result=read_json(OUT/mode/"test.json")
        require(result["commit"]==state["commit"] and result["source_files"]==state["source_files"],
                "stale input-live evidence")
        require(result["runtime_build_sha256"]==digest(rt.OUT/mode/"build.json"),
                "runtime changed after input-live test")
        require(set(result["bridge_cases"])==BRIDGE_CASES and
                set(result["live_cases"])==LIVE_CASES and
                set(result["delivery_cases"])==DELIVERY_CASES,
                "wrong input-live case set")
        require(result["physical_touch_validated"] is False,"cloud cannot validate physical touch")
        require(result["production_sha256"]==digest(rt.OUT/mode/"ui-host"),"production binary drift")
        for key,name in (("bridge_sha256","bridge-test"),("live_sha256","live-test"),("delivery_sha256","delivery-test"),("scene_sha256","touch-scene-test"),("loop_sha256","touch-loop-test")):
            require(result[key]==digest(OUT/mode/name),"test binary drift")
        for name,sha in result["logs"].items():
            require(sha==digest(OUT/mode/name),"execution log drift")
    verification={"status":"passed","commit":state["commit"],
                  "scope":"p3-02-live-evdev-pocketjs-contacts",
                  "diagnostic_transform":"1024x600-direct-requires-asymmetric-physical-check",
                  "device_selector":"ilitek_ts+protocol-b-capabilities",
                  "syn_dropped_resync":"EVIOCGMTSLOTS+current-slot",
                  "physical_touch_result":"pending-real-device",
                  "tests":{m:digest(OUT/m/"test.json") for m in ("native","arm")}}
    write_json(OUT/"verification.json",verification)
    entries=[OUT/"verification.json"]
    for mode in ("native","arm"):
        entries += [p for p in (OUT/mode).rglob("*") if p.is_file()]
    entries += [p for p in (OUT/"unit").iterdir() if p.is_file()]
    sums=OUT/"SHA256SUMS"
    sums.write_text("".join(f"{digest(p)}  {p.relative_to(OUT)}\n" for p in sorted(entries)))
    with zipfile.ZipFile(OUT/"acceptance.zip","w",zipfile.ZIP_DEFLATED) as z:
        for p in [*entries,sums]: z.write(p,str(p.relative_to(OUT)))
    print("INPUT_LIVE_EVIDENCE_OK native/ARM live fixtures + real PocketJS contact ABI; physical touch pending")

def main():
    try:
        if len(sys.argv)<2: raise RuntimeError("input_live.py unit | test native|arm | verify")
        OUT.mkdir(parents=True,exist_ok=True)
        for marker in ("verification.json","SHA256SUMS","acceptance.zip"):
            (OUT/marker).unlink(missing_ok=True)
        if sys.argv[1]=="unit" and len(sys.argv)==2: unit()
        elif sys.argv[1]=="test" and len(sys.argv)==3 and sys.argv[2] in {"native","arm"}: test(sys.argv[2])
        elif sys.argv[1]=="verify" and len(sys.argv)==2: verify()
        else: raise RuntimeError("invalid input-live command")
        return 0
    except (RuntimeError,KeyError,ValueError,OSError,subprocess.TimeoutExpired) as e:
        print(f"INPUT_LIVE_FAILED: {e}",file=sys.stderr); return 1

if __name__=="__main__": sys.exit(main())
