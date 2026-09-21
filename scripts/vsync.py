#!/usr/bin/env python3
"""P2B VSync capability evidence. Hardware timing is never inferred from fixtures."""
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

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"out/vsync"
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m
rt=load("runtime",ROOT/"scripts/runtime.py")
require,digest,write_json,read_json=rt.require,rt.digest,rt.write_json,rt.read_json
CASES={"vsync-supported","vsync-enotty-unsupported","vsync-einval-unsupported","vsync-eintr-classified",
       "vsync-timeout-bounded","vsync-error-classified","vsync-argument-bounds","vsync-json-stats"}

def run(args,log,expected=0,env=None,timeout=180):
    args=list(map(str,args)); log.parent.mkdir(parents=True,exist_ok=True)
    result=subprocess.run(args,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env,timeout=timeout)
    with log.open("a",encoding="utf-8") as f:
        f.write("$ "+shlex.join(args)+"\n"+result.stdout+f"\nexit_code={result.returncode}\n")
    print(result.stdout,end="")
    require(result.returncode==expected,f"expected {expected}, got {result.returncode}: {log}")
    return result.stdout

def validate(text,mode):
    actual=re.findall(r"^PASS ([a-z0-9-]+)$",text,re.MULTILINE)
    require(len(actual)==len(CASES) and set(actual)==CASES,"missing/duplicate vsync cases")
    require(text.count("VSYNC_TEST_OK cases=8 physical_panel_validated=false")==1,"invalid vsync summary")

def build_test(mode):
    data,_,_=rt.config(); rt.check_build(data,mode)
    directory=OUT/mode
    if directory.exists(): shutil.rmtree(directory)
    directory.mkdir(parents=True)
    tool=data["targets"][mode]; cc=rt.port.executable(tool["cc"]); runner=tool["runner"]
    flags=["-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2",*tool["c_flags"],"-Ihosts/linux"]
    sources=["hosts/linux/display/vsync.c","hosts/linux/display/fbdev.c","hosts/linux/display/presenter.c","tests/display/test_vsync.c"]
    binary=directory/"vsync-test"
    run([cc,*flags,*sources,"-Wl,--wrap=ioctl","-Wl,--gc-sections","-o",binary],directory/"build.log")
    text=run([*runner,binary],directory/"test.log")
    validate(text,mode)
    production=rt.OUT/mode/"ui-host"
    nm=rt.port.executable("arm-linux-gnueabihf-nm" if mode=="arm" else "nm")
    symbols=run([nm,production],directory/"symbols.log")
    for symbol in ("vsync_cli","vsync_probe_fd"):
        require(symbol in symbols,f"missing production symbol {symbol}")
    report=directory/"null-vsync.json"
    run([*runner,production,"--probe-vsync","--fbdev","/dev/null","--count","2","--timeout-ms","20","--output",report],directory/"cli.log",expected=1)
    evidence=read_json(report)
    require(evidence["operation"]=="vsync-probe" and evidence["status"]=="error" and evidence["writes_framebuffer"] is False,"production CLI scope mismatch")
    write_json(directory/"test.json",{**rt.project_state(),"mode":mode,"cases":sorted(CASES),"binary_sha256":digest(binary),
               "runtime_build_sha256":digest(rt.OUT/mode/"build.json"),"report_sha256":digest(report),"fixture_only":True,"physical_panel_validated":False})

def unit():
    directory=OUT/"unit"; directory.mkdir(parents=True,exist_ok=True)
    cc=rt.port.executable("gcc")
    flags=["-D_FORTIFY_SOURCE=2","-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O1","-g","-fsanitize=address,undefined","-Ihosts/linux"]
    binary=directory/"vsync-sanitized"
    sources=["hosts/linux/display/vsync.c","hosts/linux/display/fbdev.c","hosts/linux/display/presenter.c","tests/display/test_vsync.c"]
    run([cc,*flags,*sources,"-Wl,--wrap=ioctl","-o",binary],directory/"build.log")
    text=run([binary],directory/"test.log",env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1"})
    validate(text,"native")

def verify():
    state=rt.project_state(); data,_,_=rt.config()
    for mode in ("native","arm"):
        rt.check_build(data,mode); result=read_json(OUT/mode/"test.json")
        require(result["commit"]==state["commit"] and result["source_files"]==state["source_files"],"stale vsync evidence")
        require(result["runtime_build_sha256"]==digest(rt.OUT/mode/"build.json"),"runtime changed after vsync test")
        require(set(result["cases"])==CASES and result["fixture_only"] is True and result["physical_panel_validated"] is False,"wrong vsync evidence scope")
        require(result["binary_sha256"]==digest(OUT/mode/"vsync-test") and result["report_sha256"]==digest(OUT/mode/"null-vsync.json"),"vsync artifact drift")
    write_json(OUT/"verification.json",{"status":"passed","commit":state["commit"],"scope":"bounded-vsync-fixture-probe",
               "ioctl":"FBIO_WAITFORVSYNC","hardware_result":"pending-real-device","pan_enabled":False,"mode_setting":False,
               "tests":{m:digest(OUT/m/"test.json") for m in ("native","arm")}})
    entries=[OUT/"verification.json"]
    for mode in ("native","arm"):
        entries += [p for p in (OUT/mode).iterdir() if p.is_file()]
    entries += [p for p in (OUT/"unit").iterdir() if p.is_file()]
    sums=OUT/"SHA256SUMS"; sums.write_text(''.join(f'{digest(p)}  {p.relative_to(OUT)}\n' for p in sorted(entries)))
    with zipfile.ZipFile(OUT/"acceptance.zip","w",zipfile.ZIP_DEFLATED) as z:
        for p in [*entries,sums]: z.write(p,str(p.relative_to(OUT)))
    print("VSYNC_EVIDENCE_OK fixture supported/unsupported/timeout + native/ARM; hardware pending")

def main():
    try:
        OUT.mkdir(parents=True,exist_ok=True)
        if len(sys.argv)<2: raise RuntimeError("vsync.py unit | test native|arm | verify")
        if sys.argv[1]=="unit" and len(sys.argv)==2: unit()
        elif sys.argv[1]=="test" and len(sys.argv)==3 and sys.argv[2] in {"native","arm"}: build_test(sys.argv[2])
        elif sys.argv[1]=="verify" and len(sys.argv)==2: verify()
        else: raise RuntimeError("invalid vsync command")
        return 0
    except (RuntimeError,KeyError,ValueError,OSError,subprocess.TimeoutExpired) as e:
        print(f"VSYNC_FAILED: {e}",file=sys.stderr); return 1
if __name__=="__main__": sys.exit(main())
