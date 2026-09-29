#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/"out"/"ui-platform"
def run(args):
 print("+"," ".join(map(str,args)),flush=True)
 r=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 print(r.stdout,end="")
 if r.returncode: raise RuntimeError("command failed")
 return r.stdout
def compiler(mode):
 if mode=="native":return "gcc",[]
 cc=shutil.which("arm-linux-gnueabihf-gcc");q=shutil.which("qemu-arm")
 if not cc or not q:raise RuntimeError("ARM toolchain unavailable")
 return cc,["qemu-arm","-cpu","cortex-a7","-L","/usr/arm-linux-gnueabihf"]
def ctest(name,mode,sources,marker):
 OUT.mkdir(parents=True,exist_ok=True);cc,runner=compiler(mode);binary=OUT/f"{name}-{mode}"
 cmd=[cc,"-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2","-I",ROOT,*[ROOT/x for x in sources],"-o",binary]
 if mode=="arm":cmd[1:1]=["-mcpu=cortex-a7","-mfpu=neon-vfpv4","-mfloat-abi=hard"]
 run(cmd);out=run([*runner,binary])
 if marker not in out:raise RuntimeError("acceptance marker missing")
def main():
 ap=argparse.ArgumentParser();ap.add_argument("suite",choices=["layout","style","theme","golden","schema","all"]);ap.add_argument("--mode",choices=["native","arm"],default="native");a=ap.parse_args()
 suites=[a.suite] if a.suite!="all" else ["layout","style","theme","golden","schema"]
 for suite in suites:
  if suite=="layout":ctest("layout",a.mode,["tests/ui/test_layout.c","hosts/linux/ui/layout.c","hosts/linux/ui/object.c"],"LAYOUT_OK")
  elif suite=="style":ctest("style",a.mode,["tests/ui/test_style.c","hosts/linux/ui/style.c"],"STYLE_OK")
  elif suite=="theme":ctest("theme",a.mode,["tests/ui/test_theme.c","hosts/linux/ui/style.c","hosts/linux/ui/object.c"],"THEME_OK")
  elif suite=="golden":
   OUT.mkdir(parents=True,exist_ok=True);cc,runner=compiler(a.mode);binary=OUT/f"golden-{a.mode}"
   cmd=[cc,"-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2","-I",ROOT,ROOT/"tests/ui/test_layout_golden.c",ROOT/"hosts/linux/ui/layout.c",ROOT/"hosts/linux/ui/object.c","-o",binary]
   if a.mode=="arm":cmd[1:1]=["-mcpu=cortex-a7","-mfpu=neon-vfpv4","-mfloat-abi=hard"]
   run(cmd)
   docs=[]
   for h in (600,800):
    text=run([*runner,binary,str(h)]);actual=json.loads(text.strip());expected=json.loads((ROOT/f"tests/ui/golden/layout-1024x{h}.json").read_text())
    if actual!=expected:raise RuntimeError(f"layout golden mismatch {h}")
    docs.append(actual)
   if docs[0]["header"][3]!=docs[1]["header"][3] or docs[0]["body"][3]>=docs[1]["body"][3] or docs[0]["footer"][1]==docs[1]["footer"][1]:
    raise RuntimeError("dual viewport golden lost independent layout")
   print("LAYOUT_GOLDEN_OK")
  elif suite=="schema":
   for name in ("ui-layout-v1.schema.json","ui-style-v1.schema.json"):
    data=json.loads((ROOT/"schemas"/name).read_text());raw=json.dumps(data,sort_keys=True).lower()
    if data.get("$schema")!="https://json-schema.org/draft/2020-12/schema" or "lvgl" in raw or "/dev/fb" in raw or "evdev" in raw:
     raise RuntimeError("schema boundary invalid")
   print("UI_SCHEMA_OK")
if __name__=="__main__":main()
