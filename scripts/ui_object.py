#!/usr/bin/env python3
from __future__ import annotations
import argparse, shutil, subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"out"/"ui-object"

def run(args):
    print("+"," ".join(map(str,args)),flush=True)
    r=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    print(r.stdout,end="")
    if r.returncode: raise RuntimeError("command failed")
    return r.stdout

def compiler(mode):
    if mode=="native": return "gcc",[]
    cc=shutil.which("arm-linux-gnueabihf-gcc")
    qemu=shutil.which("qemu-arm")
    if not cc or not qemu: raise RuntimeError("ARM toolchain unavailable")
    return cc,["qemu-arm","-cpu","cortex-a7","-L","/usr/arm-linux-gnueabihf"]

def test(name,mode):
    OUT.mkdir(parents=True,exist_ok=True)
    cc,runner=compiler(mode)
    binary=OUT/f"{name}-{mode}"
    sources=[ROOT/"tests/ui"/f"{name}.c",ROOT/"hosts/linux/ui/object.c"]
    if name=="test_dirty":
        sources.append(ROOT/"hosts/linux/engine/fake.c")
    cmd=[cc,"-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2",
         "-I",ROOT,*sources,"-o",binary]
    if mode=="arm": cmd[1:1]=["-mcpu=cortex-a7","-mfpu=neon-vfpv4","-mfloat-abi=hard"]
    run(cmd)
    out=run([*runner,binary])
    marker={
      "test_object_model":"UI_OBJECT_MODEL_OK",
      "test_lifecycle":"UI_LIFECYCLE_OK",
      "test_dirty":"UI_DIRTY_OK"
    }[name]
    if marker not in out: raise RuntimeError("acceptance marker missing")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("suite",choices=["object","lifecycle","dirty","all"])
    ap.add_argument("--mode",choices=["native","arm"],default="native")
    a=ap.parse_args()
    names={
      "object":["test_object_model"],
      "lifecycle":["test_lifecycle"],
      "dirty":["test_dirty"],
      "all":["test_object_model","test_lifecycle","test_dirty"]
    }[a.suite]
    for name in names:test(name,a.mode)

if __name__=="__main__":
    main()
