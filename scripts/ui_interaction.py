#!/usr/bin/env python3
from __future__ import annotations
import argparse, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/"out"/"ui-interaction"
COMMON=["hosts/linux/ui/interaction.c","hosts/linux/ui/component.c","hosts/linux/ui/overlay.c",
        "hosts/linux/ui/navigation.c","hosts/linux/ui/layout.c","hosts/linux/ui/style.c",
        "hosts/linux/ui/object.c"]
def run(args):
    print("+"," ".join(map(str,args)),flush=True)
    r=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    print(r.stdout,end="")
    if r.returncode: raise RuntimeError("command failed")
    return r.stdout
def compiler(mode):
    if mode=="native": return "gcc",[]
    cc=shutil.which("arm-linux-gnueabihf-gcc");q=shutil.which("qemu-arm")
    if not cc or not q: raise RuntimeError("ARM toolchain unavailable")
    return cc,["qemu-arm","-cpu","cortex-a7","-L","/usr/arm-linux-gnueabihf"]
def test(name,mode,marker,extra):
    OUT.mkdir(parents=True,exist_ok=True);cc,runner=compiler(mode);binary=OUT/f"{name}-{mode}"
    sources=[ROOT/f"tests/ui/test_{name}.c",*[ROOT/x for x in COMMON],*[ROOT/x for x in extra]]
    cmd=[cc,"-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2","-I",ROOT,*sources,"-o",binary]
    if mode=="arm": cmd[1:1]=["-mcpu=cortex-a7","-mfpu=neon-vfpv4","-mfloat-abi=hard"]
    run(cmd);text=run([*runner,binary])
    if marker not in text: raise RuntimeError("acceptance marker missing")
def main():
    ap=argparse.ArgumentParser();ap.add_argument("suite",choices=["interaction","bridge","all"])
    ap.add_argument("--mode",choices=["native","arm"],default="native");a=ap.parse_args()
    if a.suite in ("interaction","all"): test("interaction",a.mode,"INTERACTION_OK",[])
    if a.suite in ("bridge","all"): test("interaction_bridge",a.mode,"INTERACTION_BRIDGE_OK",["hosts/linux/input/interaction_bridge.c"])
if __name__=="__main__":main()
