#!/usr/bin/env python3
"""F0 LVGL engine feasibility harness.

Native and QEMU data are software/functional evidence only. Physical i.MX6UL
remains the performance authority.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out" / "engine-spike"
LOCK = ROOT / "toolchains" / "lvgl.lock.json"
POLICY = ROOT / "experiments" / "lvgl" / "spike-policy.json"
SRC = OUT / "source" / "lvgl"
WORKLOAD = ROOT / "experiments" / "lvgl" / "workload.c"

def run(args, cwd=ROOT, env=None, timeout=600):
    print("+", " ".join(map(str, args)))
    return subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, timeout=timeout).stdout

def load(path):
    return json.loads(Path(path).read_text())

def sha256(path):
    h=hashlib.sha256()
    with open(path,"rb") as f:
        for chunk in iter(lambda:f.read(1024*1024), b""): h.update(chunk)
    return h.hexdigest()

def project_sha():
    try: return run(["git","rev-parse","HEAD"]).strip()
    except Exception: return "unknown"

def check():
    lock=load(LOCK); policy=load(POLICY)
    if lock["revision"] != "80ca777e37a2b176770726a02e07a6fb79ef0b39": raise RuntimeError("LVGL revision drift")
    if lock["tag"] != "v9.6.0" or lock["license"] != "MIT": raise RuntimeError("LVGL provenance drift")
    if policy["authority"]["imx6ul"] != "performance-authority": raise RuntimeError("hardware authority weakened")
    text=WORKLOAD.read_text()
    if re.search(r"\blv_[A-Za-z0-9_]*\b|\blvgl\b", text, re.I):
        raise RuntimeError("engine API leaked into workload")
    adapter=(ROOT/"experiments/lvgl/adapter.c").read_text()
    if "#include <lvgl/lvgl.h>" not in adapter: raise RuntimeError("adapter does not own LVGL dependency")
    print("ENGINE_SPIKE_CHECK_OK")

def fetch():
    lock=load(LOCK); OUT.mkdir(parents=True,exist_ok=True)
    if SRC.exists():
        got=run(["git","rev-parse","HEAD"],cwd=SRC).strip()
        if got==lock["revision"]:
            print("ENGINE_SPIKE_FETCH_OK cached",got); return
        shutil.rmtree(SRC)
    SRC.parent.mkdir(parents=True,exist_ok=True)
    run(["git","init",str(SRC)])
    run(["git","remote","add","origin",lock["repository"]],cwd=SRC)
    run(["git","fetch","--depth","1","origin",lock["revision"]],cwd=SRC)
    run(["git","checkout","--detach","FETCH_HEAD"],cwd=SRC)
    got=run(["git","rev-parse","HEAD"],cwd=SRC).strip()
    if got!=lock["revision"]: raise RuntimeError(f"LVGL source mismatch {got}")
    lic=SRC/"LICENCE.txt"
    if not lic.exists(): raise RuntimeError("LVGL license missing")
    (OUT/"source.json").write_text(json.dumps({
        "repository":lock["repository"],"tag":lock["tag"],"revision":got,
        "license_sha256":sha256(lic),"project_commit":project_sha()
    },indent=2,sort_keys=True)+"\n")
    print("ENGINE_SPIKE_FETCH_OK",got)

def build(mode):
    check(); fetch(); lock=load(LOCK); target=lock["targets"][mode]
    b=OUT/f"build-{mode}"
    if b.exists(): shutil.rmtree(b)
    args=["cmake","-S",ROOT/"experiments/lvgl","-B",b,
          f"-DLVGL_SOURCE_DIR={SRC}","-DCMAKE_BUILD_TYPE=Release",
          "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    if mode=="arm":
        args += [f"-DCMAKE_C_COMPILER={target['cc']}",
                 "-DCMAKE_C_FLAGS="+" ".join(target["c_flags"])]
    run(args)
    run(["cmake","--build",b,"--target","pocket-lvgl-spike","-j2"])
    binary=b/"pocket-lvgl-spike"
    if not binary.exists(): raise RuntimeError("spike binary missing")
    print("ENGINE_SPIKE_BUILD_OK",mode,sha256(binary))

def run_case(mode,width,height,scenario):
    lock=load(LOCK); b=OUT/f"build-{mode}"; binary=b/"pocket-lvgl-spike"
    if not binary.exists(): build(mode)
    d=OUT/"reports"/mode; d.mkdir(parents=True,exist_ok=True)
    report=d/f"{width}x{height}-{scenario}.json"
    cmd=[*lock["targets"][mode]["runner"],binary,
         "--width",width,"--height",height,"--scenario",scenario,
         "--duration-ms","1500","--output",report]
    text=run(cmd,timeout=120)
    if "LVGL_SPIKE_OK" not in text: raise RuntimeError("spike marker missing")
    data=load(report)
    data["execution_authority"]="software-only" if mode=="native" else "functional-only"
    data["mode"]=mode
    data["project_commit"]=project_sha()
    data["lvgl_revision"]=load(LOCK)["revision"]
    data["workload_source_sha256"]=sha256(WORKLOAD)
    data["coffee_app_sha256"]=sha256(ROOT/"apps/coffee-demo/app.js")
    data["coffee_locales_sha256"]=sha256(ROOT/"apps/coffee-demo/locales.json")
    report.write_text(json.dumps(data,indent=2,sort_keys=True)+"\n")
    return data

def test(mode):
    rows=[]
    for h in (600,800):
        for scenario in ("idle","progress"):
            rows.append(run_case(mode,1024,h,scenario))
    print(f"ENGINE_SPIKE_TEST_OK mode={mode} cases={len(rows)}")

def verify():
    policy=load(POLICY); failures=[]; summary=[]
    for mode in ("native","arm"):
        for h in (600,800):
            for scenario in ("idle","progress"):
                p=OUT/"reports"/mode/f"1024x{h}-{scenario}.json"
                if not p.exists(): failures.append(f"missing {p}"); continue
                d=load(p); summary.append(d)
                if d["handler_calls"] < 1: failures.append(f"{p}: no handler calls")
                if d["wall_ns"] <= 0 or d["cpu_ns"] < 0: failures.append(f"{p}: invalid usage")
                if scenario=="idle":
                    if d["flush_pixels"] > policy["idle"]["post_warmup_flush_pixels_max"]:
                        failures.append(f"{p}: idle redraw pixels={d['flush_pixels']}")
                    if d["wakeup_hz"] > policy["idle"]["wakeup_hz_max"]:
                        failures.append(f"{p}: wakeup_hz={d['wakeup_hz']}")
                    if d["immediate_retry_ratio"] > policy["idle"]["immediate_retry_ratio_max"]:
                        failures.append(f"{p}: retry_ratio={d['immediate_retry_ratio']}")
                    if mode=="native" and d["cpu_percent_one_core"] >= policy["idle"]["cpu_percent_one_core_max_for_native_regression"]:
                        failures.append(f"{p}: native idle cpu={d['cpu_percent_one_core']}")
                else:
                    if d["bridge_update_calls"] < 2 or d["flush_pixels"] <= 0:
                        failures.append(f"{p}: progress did not update/render")
    OUT.mkdir(parents=True,exist_ok=True)
    decision={
      "schema_version":1,
      "decision":"PENDING_PHYSICAL_EVIDENCE",
      "reason":"Native/QEMU gates cannot establish i.MX6UL CPU or visible-latency authority.",
      "lvgl_revision":load(LOCK)["revision"],
      "current_renderer_comparator":"#25",
      "physical_required":["1024x600","1024x800"],
      "cpu_guardrail":{"p5_idle_one_core_percent_max":10.0,"observation_target_percent":5.0},
      "software_cases":len(summary),
      "failures":failures
    }
    (OUT/"comparison.json").write_text(json.dumps(decision,indent=2,sort_keys=True)+"\n")
    if failures:
        print("\n".join("FAIL "+x for x in failures),file=sys.stderr)
        raise RuntimeError("engine spike verification failed")
    print("ENGINE_SPIKE_VERIFY_OK decision=PENDING_PHYSICAL_EVIDENCE")

def main():
    ap=argparse.ArgumentParser(); sub=ap.add_subparsers(dest="cmd",required=True)
    for name in ("check","fetch","verify"): sub.add_parser(name)
    for name in ("build","test"):
        p=sub.add_parser(name); p.add_argument("mode",choices=["native","arm"])
    a=ap.parse_args()
    {"check":check,"fetch":fetch,"verify":verify,
     "build":lambda:build(a.mode),"test":lambda:test(a.mode)}[a.cmd]()

if __name__=="__main__":
    main()
