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
    for symbol in ("vsync_cli","vsync_probe_fd","FBIO_WAITFORVSYNC"):
        if symbol=="FBIO_WAITFORVSYNC": continue
        require(symbol in symbols,f"missing production symbol {symbol}")
    report=directory/"null-vsync.json"
    cli=run([*runner,production,"--probe-vsync","--fbdev","/dev/null","--count","2","--timeout-ms","20","--output",report],directory/"cli.log",expected=1)
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
        require(result["binary_sha256"]==digest(OUT/mode/"vsync-test") and result["report_sha256"]==digest(OUUÛ[ÙKÈ[]Þ[ËÛÛKÞ[È\YXÝYBÜ]WÚÛÛÕUÈ\YXØ][ÛÛÛÈÝ]\È\ÜÙYÛÛ[Z]Ý]VÈÛÛ[Z]KØÛÜHÝ[Y]Þ[ËY^\K\ØH[ØÝS×ÕÐRUÔÖSÈ\Ø\WÜ\Ý[[[Ë\X[Y]XÙH[Ù[XY[ÙK[ÙWÜÙ][È[ÙK\ÝÈÛNYÙ\Ý
ÕUÛKÈ\ÝÛÛHÜH[
]]H\H__JB[Y\ÏVÓÕUÈ\YXØ][ÛÛÛBÜ[ÙH[
]]H\HN[Y\È
ÏHÜÜ[
ÕUÛ[ÙJK]\\
HY\×Ù[J
WB[Y\È
ÏHÜÜ[
ÕUÈ[]K]\\
HY\×Ù[J
WBÝ[\ÏSÕUÈÒLMÕSTÈÈÝ[\ËÜ]WÝ^
	ÉËÚ[ÞÙYÙ\Ý

_HÜ[]]WÝÊÕU
_WÈÜ[ÛÜY
[Y\ÊJJBÚ]\[K\[JÕUÈXØÙ\[ÙK\È\[KTÑQUQ
H\ÈÜ[Ê[Y\ËÝ[\×NÜ]JÝ[]]WÝÊÕU
JJB[
ÖS×ÑUQSÑWÓÒÈ^\HÝ\ÜYÝ[Ý\ÜYÝ[Y[Ý]
È]]KÐTNÈ\Ø\H[[ÈBYXZ[
NNÕUZÙ\\[ÏUYK^\ÝÛÚÏUYJBY[Þ\Ë\ÝOZ\ÙH[[YQ\ÜÞ[ËH[]\Ý]]_\H\YHBYÞ\Ë\ÝÌWOOH[][[Þ\Ë\ÝOOL[]

B[YÞ\Ë\ÝÌWOOH\Ý[[Þ\Ë\ÝOOLÈ[Þ\Ë\ÝÌH[È]]H\HNZ[Ý\Ý
Þ\Ë\ÝÌJB[YÞ\Ë\ÝÌWOOH\YH[[Þ\Ë\ÝOOL\YJ
B[ÙNZ\ÙH[[YQ\Ü[[YÞ[ÈÛÛ[X[B]\^Ù\
[[YQ\ÜÙ^Q\Ü[YQ\ÜÔÑ\ÜÝXØÙ\ÜË[Y[Ý]^\Y
H\ÈN[
ÖS×ÑRSQÙ_H[O\Þ\ËÝ\NÈ]\BY×Û[YW×ÏOH×ÛXZ[×ÈÞ\Ë^]
XZ[
JB