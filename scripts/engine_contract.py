#!/usr/bin/env python3
from __future__ import annotations
import argparse, re, shutil, subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"out"/"engine-contract"

def run(args, cwd=ROOT):
    print("+"," ".join(map(str,args)),flush=True)
    r=subprocess.run(list(map(str,args)),cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    print(r.stdout,end="")
    if r.returncode: raise RuntimeError("command failed")
    return r.stdout

def compiler(mode):
    if mode=="native": return ["gcc"], []
    cc=shutil.which("arm-linux-gnueabihf-gcc")
    qemu=shutil.which("qemu-arm")
    if not cc or not qemu: raise RuntimeError("ARM toolchain unavailable")
    return [cc], ["qemu-arm","-cpu","cortex-a7","-L","/usr/arm-linux-gnueabihf"]

def build_and_run(test, mode):
    OUT.mkdir(parents=True,exist_ok=True)
    cc,runner=compiler(mode)
    binary=OUT/f"{test}-{mode}"
    sources=[ROOT/"tests/engine"/f"{test}.c"]
    if test=="test_adapters":
        sources += [ROOT/"hosts/linux/engine/fake.c", ROOT/"hosts/linux/engine/current.c"]
    cmd=[*cc,"-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-O2",
         "-I",ROOT,*sources,"-o",binary]
    if mode=="arm":
        cmd[1:1]=["-mcpu=cortex-a7","-mfpu=neon-vfpv4","-mfloat-abi=hard"]
    run(cmd)
    text=run([*runner,binary])
    marker="ENGINE_CONTRACT_OK" if test=="test_contract" else "ENGINE_ADAPTERS_OK"
    if marker not in text: raise RuntimeError("acceptance marker missing")

def leak_check():
    paths=[
      ROOT/"hosts/linux/engine/contract.h",
      ROOT/"hosts/linux/ui",
      ROOT/"schemas",
      ROOT/"apps"
    ]
    forbidden=[
      re.compile(r"\blv_[A-Za-z0-9_]*\b"),
      re.compile(r"#\s*include\s*[<\"]lvgl(?:/|\.h)",re.I),
      re.compile(r"/dev/fb"),
      re.compile(r"\bevdev\b",re.I),
      re.compile(r"struct\s+fb_"),
    ]
    hits=[]
    for base in paths:
        candidates=[base] if base.is_file() else [p for p in base.rglob("*") if p.is_file()]
        for p in candidates:
            try:text=p.read_text(errors="ignore")
            except Exception:continue
            for rx in forbidden:
                if rx.search(text): hits.append(f"{p.relative_to(ROOT)}:{rx.pattern}")
    if hits: raise RuntimeError("engine/platform leak: "+"; ".join(hits))
    print("ENGINE_NO_LEAK_OK")

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("action",choices=["contract","adapters","leak","all"])
    ap.add_argument("--mode",choices=["native","arm"],default="native")
    a=ap.parse_args()
    if a.action in ("contract","all"): build_and_run("test_contract",a.mode)
    if a.action in ("adapters","all"): build_and_run("test_adapters",a.mode)
    if a.action in ("leak","all"): leak_check()

if __name__=="__main__":
    main()
