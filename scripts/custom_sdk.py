#!/usr/bin/env python3
"""F7-02A: bounded developer custom-component composition over existing .pui v1.

No second decoder/compiler/renderer, no native plugin, no hardware acceptance.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import public_sdk as base
import pui

ROOT=base.ROOT
CUSTOM=ROOT/"sdk/ui/js/pocket-custom.js"
WIDGET=ROOT/"sdk/ui/widgets/bar-chart.js"
APP=ROOT/"apps/custom-chart/application.js"
TYPES=ROOT/"sdk/ui/types/custom.d.ts"
CONTRACT=ROOT/"contracts/custom-surface-v0.json"
TARGETS=base.TARGETS


def need(ok,why):
    if not ok:
        raise base.PublicSDKError(why)


def check():
    parent=base.check()
    contract=base.read_json(CONTRACT)
    need(contract.get("schema")==1 and contract.get("name")=="pocket.custom.surface"
         and contract.get("protocol_version")==0
         and contract.get("base_sdk")==parent["sdk_version"]
         and contract.get("admission",{}).get("requires")=="ui.core"
         and contract["admission"].get("backend_changes") is False
         and contract["admission"].get("renderer_changes") is False
         and contract["admission"].get("hardware_execution") is False,
         "custom surface runtime/SDK compatibility claim invalid")
    expected={"max_factories":8,"max_instances_per_app":4,
              "max_primitives_per_instance":24,"max_component_size":65536,"max_style_ref":65535}
    need(contract.get("limits")==expected, "custom surface limits drift")
    need(contract.get("primitive_types")==["rectangle","horizontal-line","vertical-line"]
         and contract.get("supported_lifecycle")==["register","mount","update"],
         "custom surface claims unsupported drawing/lifecycle")
    modules=[CUSTOM,WIDGET,APP]
    for f in modules:
        need(f.is_file() and not f.is_symlink(),"missing custom SDK source")
        s=f.read_text(encoding="utf-8")
        for forbidden in (r"\b__pocketCall\b",r"\bPocketApplication\b",
                          r"\blv_[A-Za-z0-9_]+\b",r"\bJSValue\b",
                          r"hosts/linux",r"/dev/(fb|input)",r"memfd_create",
                          r"\brequire\s*\(",r"\beval\s*\(",r"\bFunction\s*\("):
            need(re.search(forbidden,s) is None,
                 "custom source crosses private boundary: "+f.name+" "+forbidden)
    widget=WIDGET.read_text()
    app=APP.read_text()
    custom=CUSTOM.read_text()
    need(widget.count('PocketCustom.register("pocket.bar-chart"')==1
         and app.count("PocketCustom.mount(")==1
         and app.count("PocketUI.define(")==1,
         "custom example must use only public registration/mount/app contracts")
    need('MAX_PRIMITIVES=24' in custom and 'MAX_INSTANCES=4' in custom
         and 'POCKET_CUSTOM_PRIMITIVE_BUDGET' in custom
         and 'POCKET_CUSTOM_BOUNDS' in custom,"missing runtime command budget guards")
    need("interface PocketCustomSurface" in TYPES.read_text()
         and "interface PocketCustomInstance" in TYPES.read_text(),"public custom types missing")
    concatenated=(
        base.FACADE.read_text()+"\n"+CUSTOM.read_text()+"\n"+
        widget+"\n"+app+"\n"
    )
    need(0<len(concatenated.encode("utf-8"))<=pui.SOURCE_LIMIT and "\x00" not in concatenated,
         "custom component source exceeds existing package source budget")
    return {"scope":"F7-02A-composition-only","public_facade":parent["sdk_version"],
            "draw_protocol":0,"native_renderer_extension":False,
            "full_polyline_supported":False,"physical_hardware":False,
            "source":{str(x.relative_to(ROOT)):base.digest(x) for x in (CUSTOM,WIDGET,APP,TYPES,CONTRACT)},
            "source_bytes":len(concatenated.encode("utf-8"))}


def package(assets:Path):
    check()
    names=base.assemble(assets)
    with tempfile.TemporaryDirectory(prefix="pocket-custom-surface-") as temp:
        dest=Path(temp)
        for name in sorted(names):
            original=assets/name
            need(original.is_file() and not original.is_symlink(),"unsafe inherited asset "+name)
            if name=="application.js":
                data=(base.FACADE.read_text()+"\n"+CUSTOM.read_text()+"\n"+
                      WIDGET.read_text()+"\n"+APP.read_text()+"\n")
                (dest/name).write_text(data,encoding="utf-8")
            else:
                shutil.copyfile(original,dest/name)
        manifest=base.read_json(base.MANIFEST)
        manifest["app_id"]="com.pocket.custom-chart"
        manifest["app_version"]="0.1.0"
        result=pui.build(manifest,dest)
        for target in TARGETS:
            v=pui.verify(result,target,allow_unsigned=True)
            need(v["capabilities"]==1 and not v["authenticated"] and not v["physical_hardware"],
                 "custom demo exceeded existing package grant")
        return result


def package_to(assets,output):
    payload=package(assets)
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open("xb") as out:out.write(payload)
    return base.digest(output)


def frame_ok(path,height):
    data=path.read_bytes()[:64]
    need(data.startswith(("P6\n1024 "+str(height)+"\n").encode()),
         "actual software framebuffer size/PPM wrong "+str(path))


def integration(assets:Path,native:Path,arm:Path,output:Path):
    check()
    need(not output.exists(),"immutable evidence output must not exist")
    for binary in (native,arm):
        need(binary.is_file() and not binary.is_symlink(),"missing real production Runtime")
    output.mkdir(parents=True)
    pkg=output/"custom-chart.pui"
    pkg_sha=package_to(assets,pkg)
    report={"scope":"existing-production-ELF-custom-Views",
            "source_commit":base.git("rev-parse","HEAD"),
            "source_tree":base.git("rev-parse","HEAD^{tree}"),
            "runtime_sha":{},"package_sha256":pkg_sha,
            "public_sdk_sha256":base.digest(base.FACADE),
            "custom_source_sha256":check()["source"],
            "real_headless_renderer":True,"real_physical_hardware":False,
            "production_admission":False,"runs":{}}
    for mode,binary in (("native",native),("arm",arm)):
        report["runtime_sha"][mode]=base.digest(binary)
        runner=[] if mode=="native" else ["qemu-arm","-cpu","cortex-a7","-L","/usr/arm-linux-gnueabihf"]
        for target in TARGETS:
            height=int(target.rsplit("x",1)[1])
            name=mode+"-"+str(height)
            inp=output/(name+".trace")
            # F6-compatible synthetic tap of chart's "Add" button; no real event
            # node is opened by --headless --replay-input.
            inp.write_text("100 1 0 110 "+str(height-90)+"\n"
                           "140 3 0 110 "+str(height-90)+"\n"
                           "280 0 0 0 0\n")
            dest=output/name
            log=output/(name+".log")
            base.invoke([*runner,binary,"--headless","--profile",target,
                         "--package",pkg,"--allow-unsigned-package",
                         "--replay-input",inp,"--seconds","1","--output",dest],log)
            result=base.read_json(dest/"report.json")
            need(result.get("ok") is True and result.get("error") is None
                 and result.get("commit")==report["source_commit"]
                 and result.get("profile")==target
                 and result.get("package_admitted") is True
                 and result.get("package_authenticated") is False
                 and result.get("package_sha256")==pkg_sha
                 and result.get("application_capabilities")==1
                 and result.get("application_heap_limit")==2097152
                 and result.get("physical_io") is False
                 and result.get("visual_validated") is False
                 and result.get("business_commands") is False
                 and result.get("core_live_bytes_after_close")==0
                 and result.get("replay_samples",0)>=3
                 and result.get("presents",0)>1,
                 "real independent chart application failed "+name)
            frame_ok(dest/"first.ppm",height)
            frame_ok(dest/"last.ppm",height)
            report["runs"][name]={
                "report":base.digest(dest/"report.json"),
                "first":base.digest(dest/"first.ppm"),
                "last":base.digest(dest/"last.ppm"),
                "replay":base.digest(inp),
                "log":base.digest(log),
                "replay_samples":result["replay_samples"],
                "presents":result["presents"],
                "actions":result.get("actions")
            }
    for height in (600,800):
        for frame in ("first","last"):
            need(report["runs"]["native-"+str(height)][frame] ==
                 report["runs"]["arm-"+str(height)][frame],
                 "Native/ARM pixels diverged: "+str(height)+"/"+frame)
        need(report["runs"]["native-"+str(height)]["first"]!=
             report["runs"]["native-"+str(height)]["last"],
             "chart click has not changed the actual painted scene")
    (output/"evidence.json").write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest="action",required=True)
    sub.add_parser("check")
    pack=sub.add_parser("pack");pack.add_argument("--assets",type=Path,required=True);pack.add_argument("--output",type=Path,required=True)
    run=sub.add_parser("integration");run.add_argument("--assets",type=Path,required=True)
    run.add_argument("--native",type=Path,required=True);run.add_argument("--arm",type=Path,required=True)
    run.add_argument("--output",type=Path,required=True)
    o=p.parse_args()
    result=check() if o.action=="check" else (
        {"package_sha256":package_to(o.assets,o.output)} if o.action=="pack" else
        integration(o.assets,o.native,o.arm,o.output))
    print(json.dumps(result,sort_keys=True))


if __name__=="__main__":
    try:main()
    except (OSError,ValueError,subprocess.SubprocessError,pui.PackageError) as exc:
        print("CUSTOM_SURFACE_FAILED:",exc,file=sys.stderr);sys.exit(1)
