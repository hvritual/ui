#!/usr/bin/env python3
from __future__ import annotations
import argparse, shutil, subprocess
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
 ap=argparse.ArgumentParser();ap.add_argument("suite",choices=["layout"]);ap.add_argument("--mode",choices=["native","arm"],default="native");a=ap.parse_args()
 if a.suite=="layout":ctest("layout",a.mode,["tests/ui/test_layout.c","hosts/linux/ui/layout.c","hosts/linux/ui/object.c"],"LAYOUT_OK")
if __name__=="__main__":main()
