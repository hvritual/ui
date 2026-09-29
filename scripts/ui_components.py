#!/usr/bin/env python3
from __future__ import annotations
import argparse, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/"out"/"ui-components"
COMMON=["hosts/linux/ui/component.c","hosts/linux/ui/navigation.c","hosts/linux/ui/overlay.c",
        "hosts/linux/ui/layout.c","hosts/linux/ui/style.c","hosts/linux/ui/object.c"]
def run(args):
 print("+"," ".join(map(str,args)),flush=True)
 r=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 print(r.stdout,end="")
 if r.returncode:raise RuntimeError("command failed")
 return r.stdout
def compiler(mode):
 if mode=="native":return "gcc",[]
 cc=shutil.which("arm-linux-gnueabihf-gcc");q=shutil.which("qemu-arm")
 if not cc or not q:raise RuntimeError("ARM toolchain unavailable")
 return cc,["qemu-arm","-cpu","cortex-a7","-L","/usr/arm-linux-gnueabihf"]
def test(name,mode,marker):
 OUT.mkdir(parents=True,exist_ok=True);cc,runner=compiler(mode);binary=OUT/f"{name}-{mode}"
 sources=[ROOT/f"tests/ui/test_{name}.c",*[ROOT/x for x in COMMON]]
 cmd=[cc,"-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2","-I",ROOT,*sources,"-o",binary]
 if mode=="arm":cmd[1:1]=["-mcpu=cortex-a7","-mfpu=neon-vfpv4","-mfloat-abi=hard"]
 run(cmd);text=run([*runner,binary])
 if marker not in text:raise RuntimeError("acceptance marker missing")
def main():
 ap=argparse.ArgumentParser();ap.add_argument("suite",choices=["components","navigation","overlay","coffee_component_migration","all"])
 ap.add_argument("--mode",choices=["native","arm"],default="native");a=ap.parse_args()
 cases={"components":"COMPONENTS_OK","navigation":"NAVIGATION_OK","overlay":"OVERLAY_OK",
        "coffee_component_migration":"COFFEE_COMPONENT_MIGRATION_OK"}
 names=list(cases) if a.suite=="all" else [a.suite]
 for name in names:test(name,a.mode,cases[name])
if __name__=="__main__":main()
