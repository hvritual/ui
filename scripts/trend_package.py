#!/usr/bin/env python3
"""Cloud-only creation of the exact tested static-ARM trend-chart board bundle."""
from pathlib import Path
import json
import shutil
import subprocess
import tarfile
import public_sdk as sdk


def main():
    root=sdk.ROOT
    out=root/"out/public-sdk"
    native=root/"out/framework/native/ui-framework"
    arm=root/"out/framework/arm/ui-framework-static"
    assets=root/"out/framework/applications/control-panel"
    baseline=sdk.integration(native,arm,assets,out/"static-baseline")
    proof=sdk.integration(native,arm,assets,out/"static-trend","trend-chart",out/"static-baseline/evidence.json")
    sdk.reject(baseline["executables"]==proof["executables"],"static baseline ELF changed")
    elf=subprocess.check_output(["arm-linux-gnueabihf-readelf","-h","-A","-l","-d",str(arm)],text=True)
    sdk.reject("ELF32" in elf and "Machine:                           ARM" in elf and
               "Tag_ABI_VFP_args: VFP registers" in elf and "INTERP" not in elf and "NEEDED" not in elf,
               "trend static ARM ABI admission")
    stage=out/"trend-board"
    stage.mkdir(exist_ok=False)
    shutil.copy2(arm,stage/"ui-framework")
    (stage/"ui-framework").chmod(0o755)
    shutil.copy2(out/"static-trend/trend-chart.pui",stage/"trend-chart.pui")
    shutil.copy2(out/"static-trend/evidence.json",stage/"software-evidence.json")
    (stage/"elf.txt").write_text(elf,encoding="utf-8")
    manifest={"schema":1,"source_commit":sdk.git("rev-parse","HEAD"),"source_tree":sdk.git("rev-parse","HEAD^{tree}"),
              "build_environment":"GitHub Actions ubuntu-22.04","physical_hardware":False,
              "samples":1024,"segments":1023,"static_arm_sha256":sdk.digest(stage/"ui-framework"),
              "package_sha256":sdk.digest(stage/"trend-chart.pui"),"software_proof_sha256":sdk.digest(stage/"software-evidence.json")}
    (stage/"manifest.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    (stage/"SHA256SUMS").write_text("".join(sdk.digest(p)+"  "+p.name+"\n" for p in sorted(stage.iterdir())),encoding="utf-8")
    archive=out/"trend-board.tar.gz"
    with tarfile.open(archive,"w:gz") as stream:
        stream.add(stage,arcname="trend-board")
    (out/"trend-board.sha256").write_text(sdk.digest(archive)+"  trend-board.tar.gz\n",encoding="utf-8")
    print("TREND_BOARD_BUNDLE_OK",json.dumps(manifest,sort_keys=True))


if __name__=="__main__":
    main()
