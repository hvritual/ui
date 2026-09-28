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
    print("+", " ".join(map(str, args)), flush=True)
    result = subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=False,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=timeout)
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n", flush=True)
    if result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, result.args, output=result.stdout)
    return result.stdout

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
    if re.search(r"\blv_[A-Za-z0-9_]*\b", text) or re.search(r"#\s*include\s*[<\"]lvgl(?:/|\.h)", text, re.I):
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
    check(); fetch(); lock=load(LOCK); target=lock["targets"]["arm" if mode=="arm-static" else mode]
    b=OUT/f"build-{mode}"
    if b.exists(): shutil.rmtree(b)
    args=["cmake","-S",ROOT/"experiments/lvgl","-B",b,
          f"-DLVGL_SOURCE_DIR={SRC}","-DCMAKE_BUILD_TYPE=Release",
          "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    if mode in ("arm","arm-static"):
        args += [f"-DCMAKE_C_COMPILER={target['cc']}",
                 f"-DCMAKE_CXX_COMPILER={target['cxx']}",
                 "-DCMAKE_C_FLAGS="+" ".join(target["c_flags"]),
                 "-DCMAKE_CXX_FLAGS="+" ".join(target["c_flags"])]
    if mode=="arm-static":
        args += ["-DCMAKE_EXE_LINKER_FLAGS=-static"]
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
    target=lock["targets"]["arm" if mode=="arm-static" else mode]
    cmd=[*target["runner"],binary,
         "--width",width,"--height",height,"--scenario",scenario,
         "--duration-ms","1500","--output",report]
    output=run(cmd,timeout=120)
    if "LVGL_SPIKE_OK" not in output: raise RuntimeError("spike marker missing")
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
                if d.get("framebuffer_active"): failures.append(f"{p}: CI case unexpectedly touched framebuffer")
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
                print("ENGINE_SPIKE_METRIC "
                      f"mode={mode} profile=1024x{h} scenario={scenario} "
                      f"cpu={d['cpu_percent_one_core']:.6f}% rss_kib={d.get('max_rss_kib',-1)} "
                      f"wake_hz={d['wakeup_hz']:.3f} handler_cpu_ns={d.get('handler_cpu_ns',-1)} "
                      f"flush_cpu_ns={d.get('flush_cpu_ns',-1)} flush_pixels={d['flush_pixels']}")
    OUT.mkdir(parents=True,exist_ok=True)
    decision={
      "schema_version":2,
      "decision":"PENDING_PHYSICAL_EVIDENCE",
      "reason":"Native/QEMU gates cannot establish i.MX6UL framebuffer CPU or visible-latency authority.",
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

def package_board():
    build("arm-static")
    b=OUT/"build-arm-static"/"pocket-lvgl-spike"
    board=OUT/"board"
    if board.exists(): shutil.rmtree(board)
    board.mkdir(parents=True)
    binary=board/"pocket-lvgl-spike-arm-static"
    shutil.copy2(b,binary); binary.chmod(0o755)
    elf=run(["arm-linux-gnueabihf-readelf","-h","-A","-l","-d",binary])
    if "Class:                             ELF32" not in elf or "Machine:                           ARM" not in elf:
        raise RuntimeError("board spike is not ELF32 ARM")
    if "Tag_ABI_VFP_args: VFP registers" not in elf:
        raise RuntimeError("board spike is not ARM hard-float")
    if "Requesting program interpreter" in elf or "(NEEDED)" in elf or "Shared library:" in elf:
        raise RuntimeError("board spike is not self-contained static ELF")
    qemu=run(["qemu-arm","-cpu","cortex-a7",binary,
              "--width","1024","--height","600","--scenario","idle",
              "--duration-ms","500","--output",board/"qemu-static-idle.json"],timeout=120)
    if "LVGL_SPIKE_OK" not in qemu: raise RuntimeError("static ARM smoke marker missing")

    script=board/"run-lvgl-fbdev-spike.sh"
    script.write_text("""#!/bin/sh
set -u
TOKEN=${1:-}
FBDEV=${2:-/dev/fb0}
PROFILE=${3:-1024x600}
DURATION=${4:-10000}
if [ "$TOKEN" != "I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER" ]; then
  echo "Refusing framebuffer write." >&2
  echo "Usage: $0 I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER [/dev/fb0] [1024x600|1024x800] [duration_ms]" >&2
  exit 2
fi
case "$PROFILE" in
  1024x600) W=1024; H=600 ;;
  1024x800) W=1024; H=800 ;;
  *) echo "Unsupported profile: $PROFILE" >&2; exit 2 ;;
esac
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$BASE/pocket-lvgl-spike-arm-static"
STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo "manual-$$")
OUT="$BASE/logs/lvgl-$PROFILE-$STAMP"
mkdir -p "$OUT"
LOG="$OUT/startup.log"
(
  echo "F0_LVGL_BOARD_BEGIN profile=$PROFILE fbdev=$FBDEV duration_ms=$DURATION"
  uname -a || true
  cat /etc/os-release 2>/dev/null || true
  cat /proc/cpuinfo 2>/dev/null || true
  cat /proc/meminfo 2>/dev/null || true
  cat /proc/loadavg 2>/dev/null || true
  echo "=== FB BLANK STATE BEFORE ==="
  cat /sys/class/graphics/$(basename "$FBDEV")/blank 2>/dev/null || true
  echo "=== FB MODE ==="
  fbset -fb "$FBDEV" 2>/dev/null || fbset 2>/dev/null || true
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$BIN"; fi
  echo "=== IDLE ==="
  "$BIN" --width "$W" --height "$H" --scenario idle --duration-ms "$DURATION" \
    --fbdev "$FBDEV" --allow-framebuffer-write I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER \
    --output "$OUT/idle.json"
  IDLE_RC=$?
  echo "IDLE_EXIT_CODE=$IDLE_RC"
  [ "$IDLE_RC" -eq 0 ] || exit "$IDLE_RC"
  cat "$OUT/idle.json"
  echo "=== FB BLANK STATE AFTER IDLE ==="
  cat /sys/class/graphics/$(basename "$FBDEV")/blank 2>/dev/null || true
  echo "=== PROGRESS ==="
  "$BIN" --width "$W" --height "$H" --scenario progress --duration-ms "$DURATION" \
    --fbdev "$FBDEV" --allow-framebuffer-write I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER \
    --output "$OUT/progress.json"
  PROGRESS_RC=$?
  echo "PROGRESS_EXIT_CODE=$PROGRESS_RC"
  [ "$PROGRESS_RC" -eq 0 ] || exit "$PROGRESS_RC"
  cat "$OUT/progress.json"
  echo "F0_LVGL_BOARD_END"
) >"$LOG" 2>&1
RC=$?
cat "$LOG"
echo "LOG_DIR=$OUT"
exit "$RC"
""",encoding="utf-8")
    script.chmod(0o755)
    readme=board/"README.txt"
    readme.write_text(
      "Pocket F0 LVGL physical framebuffer CPU spike\n\n"
      "This is a diagnostic binary, not a production runtime. It is statically linked so it does not depend on the board's uClibc loader.\n"
      "It intentionally writes /dev/fb0 and leaves the final frame visible. Stop the existing UI first.\n"
      "The binary preflights and locks the framebuffer, and only admits exact 1024x600/1024x800 32-bpp RGB layouts.\n\n"
      "Run:\n"
      "  ./run-lvgl-fbdev-spike.sh I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER /dev/fb0 1024x600 10000\n\n"
      "Return the generated logs/lvgl-*/ directory. Do not treat QEMU/native numbers as board performance.\n",
      encoding="utf-8")
    manifest={
      "schema_version":1,
      "issue":33,
      "lvgl_revision":load(LOCK)["revision"],
      "binary":binary.name,
      "binary_sha256":sha256(binary),
      "static":True,
      "dynamic_loader":None,
      "shared_libraries":[],
      "profiles":["1024x600","1024x800"],
      "fbdev_write":True,
      "render_mode":"partial",
      "buffer_rows":40,
      "physical_performance_validated":False,
      "warning":"Stop the existing framebuffer writer; test leaves final image visible."
    }
    (board/"manifest.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")
    tracked=[binary,script,readme,board/"manifest.json",board/"qemu-static-idle.json"]
    (board/"SHA256SUMS").write_text("".join(f"{sha256(p)}  {p.name}\n" for p in tracked))
    package=OUT/"pocket-lvgl-f0-imx6ul.tar.gz"
    import tarfile
    with tarfile.open(package,"w:gz",format=tarfile.PAX_FORMAT) as tar:
        for p in sorted(board.iterdir()):
            tar.add(p,arcname=Path("pocket-lvgl-f0-imx6ul")/p.name,recursive=False)
    print(f"ENGINE_SPIKE_BOARD_PACKAGE_OK sha256={sha256(package)} path={package}")
def main():
    ap=argparse.ArgumentParser(); sub=ap.add_subparsers(dest="cmd",required=True)
    for name in ("check","fetch","verify","package-board"): sub.add_parser(name)
    for name in ("build","test"):
        p=sub.add_parser(name); p.add_argument("mode",choices=["native","arm","arm-static"])
    a=ap.parse_args()
    {"check":check,"fetch":fetch,"verify":verify,"package-board":package_board,
     "build":lambda:build(a.mode),"test":lambda:test(a.mode)}[a.cmd]()

if __name__=="__main__":
    main()
