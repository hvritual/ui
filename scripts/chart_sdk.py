#!/usr/bin/env python3
"""F7-02B: real 1000-point chart app on the *existing* production UI ELF.

Public facade + chart module, existing PUI v1 container, existing bundled font.
No privileged binary or hardware input, no second compiler or renderer.
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
import public_sdk as base

ROOT=base.ROOT
CHART=ROOT/"sdk/ui/js/pocket-charts.js"
APP=ROOT/"apps/chart-1000/application.js"
MANIFEST=ROOT/"apps/chart-1000/manifest.json"
DRAW=ROOT/"hosts/linux/chart/draw.c"
TARGETS=base.TARGETS

def need(ok,message):
    if not ok:raise base.PublicSDKError(message)

def check():
    base.check()
    m=base.read_json(MANIFEST)
    pui.validate_manifest(m)
    need(m["format_version"]==1 and m["sdk_api"]==1 and
         m["runtime_api"]=={"min":1,"max":1} and
         m["targets"]==list(TARGETS) and
         m["capabilities"]==["ui.core","ui.draw.chart"] and
         m["authentication"]=="none" and
         m["budgets"]["heap_bytes"]==8388608,
         "incompatible chart manifest/version/grant")
    need(pui.CAPABILITIES.get("ui.draw.chart")==512 and
         DRAW.is_file(),"runtime chart capability missing")
    s=CHART.read_text(encoding="utf-8")
    a=APP.read_text(encoding="utf-8")
    need('const MAX_POINTS=1000' in s and 'bubble:4' in s and
         'MAX_INSTANCES=2' in s and
         'setChartData' in s and 'registerSeries' in s and
         'PocketCharts.mount(' in a and a.count('PocketCharts.mount(')==2,
         "chart module/example missing bounded 1000-point contract")
    for path,body in ((CHART,s),(APP,a)):
        for pattern in (r"\b__pocketCall\b",r"\bJSValue\b",r"\blv_\w+\b",
                        r"(?:hosts/linux|scene_guest|/dev/fb|/dev/input)",
                        r"\brequire\s*\(",r"\beval\s*\(",r"\bFunction\s*\("):
            need(re.search(pattern,body) is None,
                 "chart source crosses private boundary: "+path.name+" "+pattern)
    source=base.FACADE.read_text(encoding="utf-8")+"\n"+s+"\n"+a+"\n"
    need("\x00" not in source and len(source.encode())<=pui.SOURCE_LIMIT,
         "chart app source exceeds original R3 budget")
    files=(base.FACADE,CHART,APP,MANIFEST,DRAW,ROOT/"hosts/linux/chart/draw.h")
    return {"scope":"F7-02B-experimental-real-bounded-chart",
            "source":{str(p.relative_to(ROOT)):base.digest(p) for p in files},
            "point_capacity":1000,"physical_hardware":False,
            "signed":False,"runtime_capability":"ui.draw.chart",
            "native_chart_series":4,"bubbles":True,"source_bytes":len(source.encode())}

def package(assets:Path)->bytes:
    check()
    names=base.assemble(assets)
    with tempfile.TemporaryDirectory(prefix="pocket-chart1000-") as tmp:
        folder=Path(tmp)
        for name in sorted(names):
            p=assets/name
            need(p.is_file() and not p.is_symlink(),"unsafe source asset "+name)
            if name=="application.js":
                (folder/name).write_text(base.FACADE.read_text()+"\n"+
                    CHART.read_text()+"\n"+APP.read_text()+"\n",encoding="utf-8")
            else:shutil.copyfile(p,folder/name)
        result=pui.build(base.read_json(MANIFEST),folder)
        for target in TARGETS:
            x=pui.verify(result,target,allow_unsigned=True)
            need(x["capabilities"]==513 and
                 x["authenticated"] is False and
                 x["physical_hardware"] is False,
                 "chart runtime capability bypass")
        return result

def pack(assets:Path,output:Path):
    bytes_=package(assets)
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open("xb") as f:f.write(bytes_)
    return base.digest(output)

def ppm(path:Path,width:int,height:int)->str:
    data=path.read_bytes()
    header=("P6\n%d %d\n255\n"%(width,height)).encode()
    need(data.startswith(header) and len(data)==len(header)+width*height*3,
         "renderer did not emit complete real PPM "+str(path))
    return hashlib.sha256(data).hexdigest()

def run(assets:Path,native:Path,arm:Path,output:Path):
    need(not output.exists(),"refuse to overwrite evidence")
    for binary in (native,arm):
        need(binary.is_file() and not binary.is_symlink(),"missing real production ELF")
    output.mkdir(parents=True)
    bundle=output/"chart1000.pui"
    digest=pack(assets,bundle)
    evidence={"source_commit":base.git("rev-parse","HEAD"),
              "source_tree":base.git("rev-parse","HEAD^{tree}"),
              "scope":"real-Native-ARM-Framework-1000-point-chart",
              "runtime_builds":{},"app_sha256":digest,
              "chart_source":check()["source"],"runs":{},
              "physical_hardware":False,"production_admission":False}
    for mode,binary in (("native",native),("arm",arm)):
        evidence["runtime_builds"][mode]=base.digest(binary)
        cmd=[] if mode=="native" else [
            "qemu-arm","-cpu","cortex-a7","-L","/usr/arm-linux-gnueabihf"]
        for target in TARGETS:
            h=int(target.rsplit("x",1)[1])
            case=mode+"-"+str(h)
            trace=output/(case+".trace")
            trace.write_text("100 1 0 110 "+str(h-51)+"\n"+
                             "140 3 0 110 "+str(h-51)+"\n"+
                             "280 0 0 0 0\n")
            destination=output/case
            log=output/(case+".log")
            base.invoke([*cmd,binary,"--headless","--profile",target,
                         "--package",bundle,"--allow-unsigned-package",
                         "--replay-input",trace,"--seconds","1","--output",destination],log)
            data=base.read_json(destination/"report.json")
            need(data.get("commit")==evidence["source_commit"] and
                 data.get("profile")==target and data.get("ok") is True and
                 data.get("error") is None and data.get("package_admitted") is True and
                 data.get("package_authenticated") is False and
                 data.get("package_sha256")==digest and
                 data.get("application_capabilities")==513 and
                 data.get("application_heap_limit")==8388608 and
                 data.get("chart_series")==2 and
                 data.get("chart_points")==2000 and
                 data.get("nodes",4096)<64 and
                 data.get("physical_io") is False and
                 data.get("visual_validated") is False and
                 data.get("business_commands") is False and
                 data.get("core_live_bytes_after_close")==0 and
                 data.get("replay_samples")==3 and
                 data.get("presents",0)>=2,
                 "chart native/ARM execution failed: "+case)
            first=ppm(destination/"first.ppm",1024,h)
            last=ppm(destination/"last.ppm",1024,h)
            need(first!=last,"chart update not rendered on real Runtime: "+case)
            evidence["runs"][case]={"report":base.digest(destination/"report.json"),
                "first":first,"last":last,"log":base.digest(log),
                "points":data["chart_points"],"series":data["chart_series"],
                "nodes":data["nodes"],"replay":base.digest(trace)}
    for height in ("600","800"):
        for field in ("first","last"):
            need(evidence["runs"]["native-"+height][field]==
                 evidence["runs"]["arm-"+height][field],
                 "real renderer pixel drift native/arm "+height+"/"+field)
    (output/"evidence.json").write_text(json.dumps(evidence,indent=2,sort_keys=True)+"\n")
    print("CHART_REAL_FRAMEWORK_OK 2x1000 native+arm dual-viewport")
    return evidence

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest="action",required=True)
    sub.add_parser("check")
    packer=sub.add_parser("pack")
    packer.add_argument("--assets",type=Path,required=True)
    packer.add_argument("--output",type=Path,required=True)
    integration=sub.add_parser("integration")
    for key in ("assets","native","arm","output"):
        integration.add_argument("--"+key,type=Path,required=True)
    arg=parser.parse_args()
    result=check() if arg.action=="check" else (
        {"package_sha256":pack(arg.assets,arg.output)} if arg.action=="pack" else
        run(arg.assets,arg.native,arg.arm,arg.output))
    print(json.dumps(result,sort_keys=True))

if __name__=="__main__":
    try:main()
    except (OSError,ValueError,subprocess.SubprocessError,pui.PackageError) as exc:
        print("CHART_SDK_FAILED:",exc,file=sys.stderr)
        sys.exit(1)
