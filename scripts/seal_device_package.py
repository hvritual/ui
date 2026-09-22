#!/usr/bin/env python3
"""Bind the diagnostic distribution to the checked-out source and build evidence."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/device/imx6ul'

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> None:
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    build = ROOT / 'out/runtime/arm/build.json'
    if json.loads(build.read_text())['commit'] != commit:
        raise RuntimeError('stale ARM runtime build')
    path = OUT / 'manifest.json'
    manifest = json.loads(path.read_text())
    manifest.update(schema_version=2, source_commit=commit, runtime_build_sha256=digest(build))
    manifest['touch_test']['axis_order_verified'] = False
    manifest['touch_test']['physical_check'] = 'A-top-left B-top-right C-center D-bottom-left E-bottom-right'
    evidence = ROOT / 'out/input-live/verification.json'
    manifest['live_input_verification_sha256'] = None
    if evidence.exists():
        doc = json.loads(evidence.read_text())
        if doc['commit'] != commit or doc['status'] != 'passed':
            raise RuntimeError('stale live input verification')
        manifest['live_input_verification_sha256'] = digest(evidence)
        (OUT / 'live-input-verification.json').write_bytes(evidence.read_bytes())
    path.write_text(json.dumps(manifest, indent=2) + '\n')
    notes = OUT / 'README.txt'
    notes.write_text(notes.read_text() + '\nP3 acceptance: tap A, B, C, D, E and verify markers follow the finger. Repeat a tap, hold and release, then drag across targets. No physical acceptance is inferred from program exit status.\nReturn logs/touch-*/ including input-guest-present.csv; present timestamps describe CPU submission, not LCD scanout.\n')
    paths = sorted(p for p in OUT.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
    (OUT / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.relative_to(OUT)}\n' for p in paths))
    package = ROOT / 'out/device/imx6ul-device-test.tar.gz'
    with tarfile.open(package, 'w:gz') as archive:
        for p in sorted(OUT.rglob('*')):
            archive.add(p, arcname=Path('imx6ul-device-test') / p.relative_to(OUT), recursive=False)
    print(f'DEVICE_PROVENANCE_OK source_commit={commit} package_sha256={digest(package)}')

if __name__ == '__main__':
    main()
