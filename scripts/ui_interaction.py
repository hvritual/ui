#!/usr/bin/env python3
from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/"out"/"ui-interaction"
COMMON=["hosts/linux/ui/interaction.c","hosts/linux/ui/component.c","hosts/linux/ui/overlay.c",
        "hosts/linux/ui/navigation.c","hosts/linux/ui/layout.c","hosts/linux/ui/style.c",
        "hosts/linux/ui/object.c"]
def run(args, expected=0):
    print("+"," ".join(map(str,args)),flush=True)
    r=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    print(r.stdout,end="")
    if r.returncode!=expected: raise RuntimeError("command failed")
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
    if name=="gesture":
        negative=run([*runner,binary,"--intentional-failure"],expected=1)
        if "GESTURE_FAIL" not in negative: raise RuntimeError("negative control failed open")
    if marker not in text: raise RuntimeError("acceptance marker missing")
    if name=="gesture":
        for height in (600,800):
            command=[*runner,binary,"--replay",ROOT/"tests/ui/fixtures/interaction.csv",str(height)]
            first=run(command);second=run(command)
            if first!=second or "REPLAY_OK" not in first: raise RuntimeError("replay not deterministic")
            (OUT/f"replay-{height}-{mode}.log").write_text(first)
            if mode=="arm":
                native=OUT/f"replay-{height}-native.log"
                if not native.is_file() or native.read_text()!=first:
                    raise RuntimeError("native/ARM replay parity missing or failed")
    if mode=="native" and os.environ.get("POCKET_INTERACTION_SANITIZE")=="1":
        sanitized=OUT/f"{name}-sanitized"
        run([cc,"-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O1","-g",
             "-fno-omit-frame-pointer","-fsanitize=address,undefined","-fno-sanitize-recover=all",
             "-I",ROOT,*sources,"-o",sanitized])
        run([sanitized])
    return binary
def main():
    ap=argparse.ArgumentParser();ap.add_argument("suite",choices=["interaction","bridge","gesture","all"])
    ap.add_argument("--mode",choices=["native","arm"],default="native");a=ap.parse_args()
    if a.suite in ("interaction","all"): test("interaction",a.mode,"INTERACTION_OK",[])
    if a.suite in ("bridge","all"): test("interaction_bridge",a.mode,"INTERACTION_BRIDGE_OK",["hosts/linux/input/interaction_bridge.c"])
    if a.suite in ("gesture","all"): test("gesture",a.mode,"GESTURE_OK",["hosts/linux/input/interaction_bridge.c"])
if __name__=="__main__":main()
