#!/usr/bin/env python3
"""Verify downloaded physical trend-chart evidence against the cloud-built bundle."""
import argparse
import json
from pathlib import Path
import public_sdk as sdk


def verify(bundle: Path, output: Path):
    manifest=sdk.read_json(bundle/"manifest.json")
    report=sdk.read_json(output/"report.json")
    display=sdk.read_json(output/"display.json")
    sdk.reject(sdk.digest(bundle/"ui-framework")==manifest["static_arm_sha256"] and
               sdk.digest(bundle/"trend-chart.pui")==manifest["package_sha256"],"downloaded cloud bundle hashes")
    for line in (bundle/"SHA256SUMS").read_text(encoding="utf-8").splitlines():
        expected,name=line.split("  ",1)
        sdk.reject(name==Path(name).name and sdk.digest(bundle/name)==expected,"cloud bundle member hash")
    sdk.reject(report.get("commit")==manifest["source_commit"] and report.get("profile")==sdk.TARGETS[0] and
               report.get("ok") is True and report.get("error") is None and report.get("physical_io") is True and
               report.get("business_commands") is False and report.get("package_authenticated") is False and
               report.get("package_sha256")==manifest["package_sha256"] and
               report.get("drawing_points")==1024 and report.get("drawing_surfaces")==1 and
               report.get("drawing_commits")==2 and report.get("drawing_uploads")==2 and
               report.get("drawing_uploaded_points")==2048 and report.get("item_count")==1024 and
               report.get("nodes")==8 and report.get("presents",0)>=2 and report.get("bytes_written",0)>0 and
               report.get("core_live_bytes_after_close")==0,"physical 1024-point Runtime admission")
    pixels=sdk.trend_pixels(output,600)
    info=display.get("info")
    sdk.reject(display.get("admitted") is True and display.get("mode_changed_by_host") is False and
               info and info["xres"]==1024 and info["yres"]==600 and info["bits_per_pixel"] in (16,32),
               "observed physical display geometry")
    raw=(output/"framebuffer.raw").read_bytes()
    stride=info["line_length"];bpp=info["bits_per_pixel"]//8
    needed=(info["yoffset"]+600-1)*stride+(info["xoffset"]+1024)*bpp
    sdk.reject(len(raw)>=needed,"truncated physical framebuffer readback")
    rgb=bytearray(1024*600*3)
    for y in range(600):
        for x in range(1024):
            at=(y+info["yoffset"])*stride+(x+info["xoffset"])*bpp
            value=int.from_bytes(raw[at:at+bpp],"little")
            for c,name in enumerate(("red","green","blue")):
                field=info[name]
                sdk.reject(not field["msb_right"] and field["length"] in (5,6,8),"unsupported physical bitfield")
                maximum=(1<<field["length"])-1
                rgb[(y*1024+x)*3+c]=((value>>field["offset"])&maximum)*255//maximum
    expected=(output/"last.ppm").read_bytes().split(b"\n",3)[3]
    sdk.reject(bytes(rgb)==expected,"physical framebuffer differs from final rendered image")
    (output/"framebuffer.ppm").write_bytes(b"P6\n1024 600\n255\n"+rgb)
    result={"schema":1,"source_commit":manifest["source_commit"],"source_tree":manifest["source_tree"],
            "build_environment":manifest["build_environment"],"static_arm_sha256":manifest["static_arm_sha256"],
            "package_sha256":manifest["package_sha256"],"physical_hardware":True,"profile":sdk.TARGETS[0],
            "samples":1024,"segments":1023,"pixels":pixels,"framebuffer_readback_matches":True,
            "cpu_percent_one_core":report["cpu_percent_one_core"],"peak_rss_kib":report["peak_rss_kib"],
            "presents":report["presents"],"general_performance_admission":False,"physical_800_verified":False,
            "files":{p.name:sdk.digest(p) for p in sorted(output.iterdir()) if p.is_file() and p.name!="physical-evidence.json"}}
    (output/"physical-evidence.json").write_text(json.dumps(result,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    return result


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    options=parser.parse_args()
    print(json.dumps(verify(options.bundle,options.output),sort_keys=True))
