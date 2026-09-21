#!/usr/bin/env python3
"""Framebuffer evidence: real PocketJS renderer, explicitly synthetic fbdev I/O."""
from __future__ import annotations
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

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out/display"
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module); return module
rt = load("runtime", ROOT / "scripts/runtime.py")
assets = load("display_assets", ROOT / "tests/display/assets.py")
require, digest, read_json, write_json = rt.require, rt.digest, rt.read_json, rt.write_json
WRAPS = ["open", "__open_2", "fstat", "flock", "ioctl", "mmap", "munmap", "close", "sysconf"]
UNIT_CASES = set("""xrgb8888-padding-offset-canaries all-32bit-byte-orders-opaque-alpha
 offset-grid-preserves-hidden-pixels rgb565-exact-quantization reject-24bpp reject-directcolor
 reject-planar reject-grayscale-fourcc reject-nonstandard reject-overlapping-channels reject-msb-right
 reject-nonbyte-channels reject-overlapping-alpha reject-rotation reject-ywrap-interlace
 reject-zero-dimension reject-offset-outside-virtual reject-short-stride reject-short-smem
 reject-huge-map reject-virtual-overflow reject-physical-address-overflow reject-source-map-alias-without-writes
 pan-is-only-a-reported-candidate read-only-probe-no-mapping-or-writes device-lifecycle-descriptor-zero
 open-permission-denied stat-failure-cleanup reject-regular-file-device exclusive-lock-failure
 fixed-info-failure variable-info-failure mmap-failure-cleanup unaligned-smem-rejected page-size-failure
 mode-change-fails-before-writing requery-error-latches unmap-error-retains-owner-for-retry
 close-eintr-not-retried json-probe-escape-and-unknown-sync""".split())
CORE_CASES = {f"core-card-{h}-{b}-{state}" for h in (600,800) for b in (16,32) for state in ("initial","updated")}
CORE_CASES |= {f"present-clock-pause-{h}-{b}" for h in (600,800) for b in (16,32)}
CORE_CASES |= {"callback-failure-latches-host", "wrong-panel-profile-no-scaling", "cli-read-only-probe-exclusive-report",
               "cli-display-test-real-core", "cli-reject-invalid-options", "real-core-display-cleanup"}


def config(data=None):
    data = read_json(ROOT / "targets/display.json") if data is None else data
    require(data["schema_version"] == 1 and data["renderer_format"] == "BGRA8888-opaque", "wrong renderer contract")
    require(data["presenter"] == "fbdev-mmap-row-copy" and data["logical_scale"] == 1 and data["rotation_degrees"] == 0, "unverified presentation transform")
    require(data["maximum_mapping_bytes"] == 64*1024*1024, "mapping budget drift")
    require(data["supported_formats"] == ["32bit-byte-aligned-RGB888-X-or-A8", "RGB565-little-endian"], "format drift")
    require(all(data[key] is False for key in ("set_display_mode", "pan_enabled", "vsync_enabled")), "unsafe hardware capability claim")
    require(data["hardware_admission"] == "blocked-pending-device-ABI-and-display-evidence", "hardware has not been tested")
    expected = read_json(rt.PROFILES)["profiles"]
    require(data["profiles"] == [{**p, "display_probe": None} for p in expected], "independent display profiles required")
    return data


def run(args, logfile, expected=0, timeout=180, env=None):
    args = list(map(str,args)); logfile.parent.mkdir(parents=True,exist_ok=True)
    with logfile.open("a",encoding="utf-8") as log:
        log.write("$ " + shlex.join(args) + "\n"); log.flush()
        result = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=timeout, env=env)
        log.write(result.stdout + f"\nexit_code={result.returncode}\n")
    print(result.stdout,end="",flush=True)
    require(result.returncode == expected, f"expected exit {expected}, got {result.returncode}: {logfile}")
    return result.stdout


def validate_output(text, mode, core=False):
    cases = CORE_CASES if core else UNIT_CASES
    actual = re.findall(r"^PASS ([a-z0-9-]+)$",text,re.MULTILINE)
    require(len(actual)==len(cases) and set(actual)==cases, "missing/duplicate display cases")
    prefix = "DISPLAY_CORE_OK" if core else "PRESENTER_OK"
    require(text.count(f"{prefix} pointer_bits={32 if mode=='arm' else 64}")==1 and
            "physical_panel_validated=false" in text and "DISPLAY_TEST_FAILED" not in text, "invalid display summary")


def golden_files(directory):
    for h in (600,800):
        for t in (0,2):
            rgb = assets.reference(h,t)
            for b in (16,32):
                if b==16:
                    rgb16=bytearray(rgb)
                    for i,c in enumerate(rgb16):
                        shift=2 if i%3==1 else 3; bits=c>>shift
                        rgb16[i]=(bits<<shift)|(bits>>(8-2*shift))
                    body=bytes(rgb16)
                else: body=rgb
                expected=f"P6\n1024 {h}\n255\n".encode()+body
                path=directory / f"observed-{h}-{t}-{b}.ppm"
                require(path.read_bytes()==expected, f"independent full-frame oracle mismatch: {path.name}")


def unit():
    config(); directory=OUT / "unit"; directory.mkdir(parents=True,exist_ok=True)
    run([sys.executable,"-m","unittest","discover","-s","tests","-p","test_display.py","-v"],directory/"python.log")
    cc=rt.port.executable("gcc")
    flags=["-D_FORTIFY_SOURCE=2","-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O1","-g","-fsanitize=address,undefined",
           "-Ihosts/linux","-Itests/display"]
    sources=["hosts/linux/display/presenter.c","hosts/linux/display/fbdev.c","tests/display/fake_fbdev.c","tests/display/test_presenter.c"]
    binary=directory/"presenter-sanitized"
    run([cc,*flags,*sources,*["-Wl,--wrap="+w for w in WRAPS],"-o",binary],directory/"build.log")
    text=run([binary,directory/"probe.json"],directory/"test.log",env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1"})
    validate_output(text,"native")
    negative=run([binary,"--intentional-failure"],directory/"negative.log",expected=1)
    require("DISPLAY_TEST_FAILED" in negative and "PRESENTER_OK" not in negative,"sanitizer negative failed open")


def test(mode):
    config(); data,base,_=rt.config(); runtime_build=rt.check_build(data,mode)
    directory=OUT/mode
    if directory.exists(): shutil.rmtree(directory)
    directory.mkdir(parents=True); fixtures=OUT/"fixtures"; assets.write_assets(fixtures)
    tool=data["targets"][mode]; cc=rt.port.executable(tool["cc"]); runner=tool["runner"]
    state=rt.project_state(); prepared=rt.OUT/("source-"+mode); quickjs=rt.port.checked_sources(base)
    core=rt.OUT/"cargo"/tool["triple"]/"release/libpocketjs_symbian_core.a"
    flags=["-O2","-ffunction-sections","-fdata-sections",*tool["c_flags"],
           '-DPOCKETJS_TARGET_ID="linux-headless"',"-DPOCKETJS_HOST_ABI=1"]
    includes=[ROOT/"hosts/linux",ROOT/"tests/display",rt.OUT/"include",quickjs,
              prepared/"engine/quickjs-c",prepared/"engine/ui-cabi/include",prepared/"contracts/generated"]
    for path in includes: flags += ["-I",str(path)]
    source_paths=[ROOT/"hosts/linux/host.c",ROOT/"hosts/linux/platform.c",
                  *[ROOT/"hosts/linux/display"/(n+".c") for n in ("presenter","fbdev","cli")],
                  prepared/"engine/quickjs-c/pocket_runtime.c",prepared/"engine/quickjs-c/rust_eh_personality.c",
                  ROOT/"tests/display/fake_fbdev.c"]
    objects={}
    for path in source_paths:
        obj=directory/(path.stem+".o"); objects[path.stem]=obj
        strict=["-std=c11","-Wall","-Wextra","-Werror"] if path.is_relative_to(ROOT/"hosts") or path.is_relative_to(ROOT/"tests") else ["-std=gnu11"]
        run([cc,*flags,*strict,"-c",path,"-o",obj],directory/"build.log")
    for name,source,selected in [
        ("presenter-test","test_presenter.c",[objects[n] for n in ("presenter","fbdev","fake_fbdev")]),
        ("display-core-test","test_display.c",list(objects.values()))]:
        cmd=[cc,*flags,"-std=c11","-Wall","-Wextra","-Werror",ROOT/"tests/display"/source,*selected]
        if name=="display-core-test": cmd += [rt.OUT/mode/"libquickjs.a",core,"-lm","-ldl","-lpthread","-lrt"]
        run([*cmd,*["-Wl,--wrap="+w for w in WRAPS],"-Wl,--gc-sections","-o",directory/name],directory/"build.log")
    shutil.copy2(rt.OUT/mode/"ui-host",directory/"ui-host")
    symbols=run(["arm-linux-gnueabihf-nm" if mode=="arm" else "nm",directory/"ui-host"],directory/"symbols.log")
    for name in ("fbdev_present","display_cli","fb_copy","pocket_runtime_tick"): require(name in symbols,"missing production display symbol")
    require("fake_fb" not in symbols and "__wrap_" not in symbols,"fixture hooks leaked into production")
    binaries={name:digest(directory/name) for name in ("ui-host","presenter-test","display-core-test")}
    write_json(directory/"build.json",{**state,"mode":mode,"runtime_build_sha256":digest(rt.OUT/mode/"build.json"),
               "fixtures":rt.file_hashes(fixtures),"binaries":binaries,"compiled_sources":{str(p.relative_to(ROOT)):digest(p) for p in source_paths},
               "objects":{n:digest(p) for n,p in objects.items()},"display_config_sha256":digest(ROOT/"targets/display.json"),
               "real_core":True,"framebuffer_io":"fixture","physical_panel_validated":False})
    text=run([*runner,directory/"presenter-test",directory/"probe-fixture.json"],directory/"unit.log")
    validate_output(text,mode); (directory/"unit.stdout").write_text(text)
    probe=read_json(directory/"probe-fixture.json")
    require(probe["device"]=='/dev/fb"\\\n' and probe["presents"]==0 and probe["presentation"]["vsync"]=="not-probed","probe serialization mismatch")
    negative=run([*runner,directory/"presenter-test","--intentional-failure"],directory/"unit-negative.log",expected=1)
    require("DISPLAY_TEST_FAILED" in negative and "PRESENTER_OK" not in negative,"unit negative failed open")
    (directory/"unit-negative.stdout").write_text(negative)
    text=run([*runner,directory/"display-core-test",fixtures,directory],directory/"core.log")
    validate_output(text,mode,True); (directory/"core.stdout").write_text(text); golden_files(directory)
    negdir=directory/"negative"; negdir.mkdir()
    negative=run([*runner,directory/"display-core-test",fixtures,negdir,"--intentional-failure"],directory/"core-negative.log",expected=1)
    require("GOLDEN_MISMATCH" in negative and "DISPLAY_CORE_OK" not in negative,"golden negative failed open")
    (directory/"core-negative.stdout").write_text(negative)
    cli=run([*runner,directory/"ui-host","--probe-display","--fbdev","/dev/null"],directory/"production-cli.log",expected=1)
    require('"error":"FB_NOT_FRAMEBUFFER"' in cli,'production probe did not reject /dev/null')
    (directory/"production-cli.stdout").write_text(cli)
    results={p.name:digest(p) for p in directory.iterdir() if p.suffix in {".stdout",".ppm",".json"} and p.name!="test.json"}
    write_json(directory/"test.json",{"commit":state["commit"],"mode":mode,"build_sha256":digest(directory/"build.json"),
               "returncode":0,"unit_negative_returncode":1,"core_negative_returncode":1,"unit_cases":sorted(UNIT_CASES),
               "core_cases":sorted(CORE_CASES),"outputs":results,"physical_panel_validated":False})


def verify():
    config(); data,_,_=rt.config(); state=rt.project_state()
    for mode in ("native","arm"):
        rt.check_build(data,mode); directory=OUT/mode
        build=read_json(directory/"build.json"); result=read_json(directory/"test.json")
        require(build["commit"]==result["commit"]==state["commit"] and build["source_files"]==state["source_files"],"stale display source")
        require(build["runtime_build_sha256"]==digest(rt.OUT/mode/"build.json"),"runtime changed after display build")
        require(result["build_sha256"]==digest(directory/"build.json"),"display test not bound to build")
        require(build["fixtures"]==rt.file_hashes(OUT/"fixtures"),"fixture drift")
        require(build["display_config_sha256"]==digest(ROOT/"targets/display.json"),"display configuration drift")
        for name,sha in build["binaries"].items(): require(digest(directory/name)==sha,"display binary drift")
        require(set(build["binaries"])=={"ui-host","presenter-test","display-core-test"},"binary set incomplete")
        require(set(result["unit_cases"])==UNIT_CASES and set(result["core_cases"])==CORE_CASES,"test set incomplete")
        require(result["returncode"]==0 and result["unit_negative_returncode"]==result["core_negative_returncode"]==1,"bad exit status")
        require(result["physical_panel_validated"] is False and build["real_core"] is True and build["framebuffer_io"]=="fixture","wrong evidence scope")
        required={"unit.stdout","core.stdout","unit-negative.stdout","core-negative.stdout","production-cli.stdout","build.json","probe-fixture.json","cli-probe.json","cli-display.json"}
        required|={f"observed-{h}-{t}-{b}.ppm" for h in (600,800) for t in (0,2) for b in (16,32)}
        require(set(result["outputs"])==required,"missing display raw output")
        for name,sha in result["outputs"].items(): require(digest(directory/name)==sha,"display output drift")
        validate_output((directory/"unit.stdout").read_text(),mode); validate_output((directory/"core.stdout").read_text(),mode,True)
        golden_files(directory)
    p1=read_json(rt.OUT/"verification.json")
    require(p1["commit"]==state["commit"] and p1["status"]=="passed","P1 regression evidence required")
    write_json(OUT/"verification.json",{"status":"passed","commit":state["commit"],"scope":"real-core-to-fixture-framebuffer",
               "unit_cases_per_target":len(UNIT_CASES),"core_cases_per_target":len(CORE_CASES),"physical_panel_validated":False,
               "vsync":"not-probed","pan":"disabled","board_abi":"unverified","p1_verification_sha256":digest(rt.OUT/"verification.json"),
               "tests":{m:digest(OUT/m/"test.json") for m in ("native","arm")}})
    shutil.copytree(rt.OUT/"licenses",OUT/"licenses",dirs_exist_ok=True)
    shutil.copy2(ROOT/"docs/display.md",OUT/"README.md")
    run(["git","archive","--format=tar","-o",OUT/"project-source.tar","HEAD"],OUT/"source.log")
    entries=[OUT/"verification.json",OUT/"README.md",OUT/"project-source.tar",OUT/"source.log"]
    entries += list((OUT/"fixtures").iterdir()) + list((OUT/"licenses").iterdir())
    for mode in ("native","arm"):
        entries += [p for p in (OUT/mode).iterdir() if p.is_file() and (p.suffix in {".json",".stdout",".log",".ppm"} or p.name in {"ui-host","presenter-test","display-core-test"})]
    sums=OUT/"SHA256SUMS"; sums.write_text(''.join(f'{digest(p)}  {p.relative_to(OUT)}\n' for p in sorted(entries)))
    with zipfile.ZipFile(OUT/"acceptance.zip","w",zipfile.ZIP_DEFLATED) as package:
        for p in [*entries,sums]: package.write(p,str(p.relative_to(OUT)))
    print("DISPLAY_EVIDENCE_OK real-core + native/ARM fixture display; physical panel unverified")


def main():
    try:
        OUT.mkdir(parents=True,exist_ok=True)
        for name in ("verification.json","SHA256SUMS","acceptance.zip"): (OUT/name).unlink(missing_ok=True)
        require(len(sys.argv) in {2,3},"display.py unit | test native|arm | verify")
        action=sys.argv[1]
        if action=="unit" and len(sys.argv)==2: unit()
        elif action=="test" and len(sys.argv)==3 and sys.argv[2] in {"native","arm"}: test(sys.argv[2])
        elif action=="verify" and len(sys.argv)==2: verify()
        else: raise RuntimeError("invalid display command")
        return 0
    except (RuntimeError,KeyError,ValueError,OSError,subprocess.TimeoutExpired) as error:
        for name in ("verification.json","SHA256SUMS","acceptance.zip"): (OUT/name).unlink(missing_ok=True)
        with (OUT/"failures.log").open("a") as f: f.write(f"DISPLAY_FAILED: {error}\n")
        print(f"DISPLAY_FAILED: {error}",file=sys.stderr); return 1
if __name__=="__main__": sys.exit(main())
