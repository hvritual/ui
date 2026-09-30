#!/usr/bin/env python3
"""Exact-source native text-session checks; no renderer or board admission."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/text-input'
LOCK = ROOT / 'toolchains/text-input.lock.json'

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def blob_hash(data: bytes) -> str:
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()

def source() -> dict:
    files = subprocess.check_output(['git', 'ls-files'], cwd=ROOT, text=True).splitlines()
    return {'commit': subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'source_files': {p: digest((ROOT/p).read_bytes()) for p in files if (ROOT/p).is_file()}}

def get_file(url: str, path: Path, expected: str) -> None:
    if path.exists():
        if blob_hash(path.read_bytes()) != expected:
            raise ValueError('cached dependency integrity mismatch: ' + path.name)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                data = response.read(3_000_001)
            if len(data) > 3_000_000 or blob_hash(data) != expected:
                raise ValueError('downloaded dependency integrity mismatch: ' + path.name)
            tmp = path.with_suffix(path.suffix + '.tmp')
            tmp.write_bytes(data)
            tmp.replace(path)
            return
        except OSError:
            if attempt == 2:
                raise
            time.sleep(1)

def dependencies() -> Path:
    lock = json.loads(LOCK.read_text())
    dep = OUT / 'deps'
    for name, expected in lock['git_blob_sha1'].items():
        get_file(lock['base_url'] + name, dep/name, expected)
    case = lock['conformance']
    get_file(case['url'], dep/case['name'], case['git_blob_sha1'])
    vectors = []
    for raw in (dep/case['name']).read_text().splitlines():
        tokens = raw.split('#', 1)[0].split()
        if not tokens:
            continue
        data = b''
        boundaries = []
        for token in tokens:
            if token == '÷':
                boundaries.append(len(data))
            elif token != '×':
                data += chr(int(token, 16)).encode('utf-8')
        if not boundaries or boundaries[0] != 0 or boundaries[-1] != len(data):
            raise ValueError('invalid Unicode conformance source')
        vectors.append(f'{len(data)} {len(boundaries)}\n' + data.hex(' ') + '\n' +
                       ' '.join(map(str, boundaries)) + '\n')
    if len(vectors) < 500:
        raise ValueError('incomplete conformance matrix')
    (dep/'grapheme-vectors.txt').write_text(''.join(vectors))
    return dep

def matrix_check(matrix: dict) -> None:
    required = {'en-US','zh-CN','de-DE','fr-FR','es-ES','pt-PT','zh-TW','ja-JP','ko-KR','ar','th-TH'}
    rows = matrix['rows']
    if matrix['schema_version'] != 1 or len(rows) != len(required) or {r['locale'] for r in rows} != required:
        raise ValueError('missing/duplicate language row')
    for row in rows:
        expected_layout = 'software-ascii-qwerty' if row['locale'] == 'en-US' else 'not-implemented'
        if row['product_input_admitted'] is not False or row['layout_status'] != expected_layout or row['dictionary'] is not None:
            raise ValueError('unverified product locale admission')
        if row['ime_status'] not in ('not-required', 'not-implemented'):
            raise ValueError('unverified IME admission')
    if any(matrix['sensitive_policy'].values()):
        raise ValueError('unsafe sensitive policy')

def matrix_tests() -> None:
    m = json.loads((ROOT/'assets/locales/input-capabilities.json').read_text())
    matrix_check(m)
    for mutation in ('admit', 'missing', 'privacy'):
        bad = json.loads(json.dumps(m))
        if mutation == 'admit': bad['rows'][0]['product_input_admitted'] = True
        elif mutation == 'missing': bad['rows'].pop()
        else: bad['sensitive_policy']['log_text'] = True
        try:
            matrix_check(bad)
        except ValueError:
            continue
        raise ValueError('matrix negative gate failed open')
    print('LANGUAGE_MATRIX_OK admitted=0 planned_rows=11 negative_cases=3')

def run(args: list, log: Path, expected: int = 0, env: dict | None = None) -> str:
    command = list(map(str,args))
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=180, env=env)
    log.write_text('$ ' + ' '.join(command) + '\n' + result.stdout + f'\nexit={result.returncode}\n')
    if result.returncode != expected:
        print(result.stdout, file=sys.stderr)
        raise ValueError(f'command failed: {log.name}, exit={result.returncode}, expected={expected}')
    return result.stdout

def test(mode: str) -> None:
    matrix_tests()
    dep = dependencies()
    before = source()
    dest = OUT/mode
    dest.mkdir(parents=True, exist_ok=True)
    arm = mode == 'arm'
    sanitizer = mode == 'sanitize'
    cc = 'arm-linux-gnueabihf-gcc' if arm else 'gcc'
    flags = ['-static','-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard'] if arm else []
    if sanitizer:
        flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-g']
    binary = dest/'text-session-test'
    run([cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-O1','-DUTF8PROC_STATIC',
         '-I.', '-I'+str(dep), *flags, 'hosts/linux/text-input/session.c',
         'tests/text-input/test_session.c', dep/'utf8proc.c','-o',binary], dest/'build.log')
    prefix = ['qemu-arm','-cpu','cortex-a7'] if arm else []
    args = [*prefix,binary,dep/'grapheme-vectors.txt']
    env = {**os.environ, 'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS':'halt_on_error=1'}
    output = run(args,dest/'test.log',env=env)
    repeated = run(args,dest/'repeat.log',env=env)
    negative = run([*args,'--negative'],dest/'negative.log',1,env)
    if output != repeated or 'TEXT_SESSION_OK groups=7' not in output or 'TEXT_UNICODE_OK version=17.0.0' not in output:
        raise ValueError('incomplete or nondeterministic text suite')
    if 'TEXT_INTENTIONAL_NEGATIVE' not in negative or 'TEXT_SESSION_OK' in negative:
        raise ValueError('negative gate did not fail at the intended assertion')
    if before != source():
        raise ValueError('source changed during test')
    if arm:
        elf = run(['arm-linux-gnueabihf-readelf','-h','-l','-A','-d',binary],dest/'elf.log')
        if 'ELF32' not in elf or 'Tag_ABI_VFP_args: VFP registers' not in elf or 'INTERP' in elf or 'NEEDED' in elf:
            raise ValueError('static ARM ABI gate failed')
    (dest/'result.json').write_text(json.dumps({**before,'mode':mode,'status':'passed',
        'scope':'headless-text-session-only','physical_hardware':False,'keyboard_ui':False,'offline_ime':False,
        'binary_sha256':digest(binary.read_bytes()),'output':output,
        'dependencies':{p.name:digest(p.read_bytes()) for p in sorted(dep.iterdir()) if p.is_file()},
        'logs':{p.name:digest(p.read_bytes()) for p in sorted(dest.glob('*.log'))}},indent=2)+'\n')
    print(output, end='')

def verify() -> None:
    current=source()
    reports=[]
    for mode in ('native','arm','sanitize'):
        d=OUT/mode
        r=json.loads((d/'result.json').read_text())
        if r['status']!='passed' or r['commit']!=current['commit'] or r['source_files']!=current['source_files']:
            raise ValueError('stale text input evidence')
        if r['physical_hardware'] or r['keyboard_ui'] or r['offline_ime']:
            raise ValueError('invalid scope claim')
        if r['binary_sha256']!=digest((d/'text-session-test').read_bytes()):
            raise ValueError('binary drift')
        for name,expected in r['dependencies'].items():
            if digest((OUT/'deps'/name).read_bytes())!=expected: raise ValueError('dependency drift')
        for name,expected in r['logs'].items():
            if digest((d/name).read_bytes())!=expected: raise ValueError('log drift')
        reports.append(r)
    if len({r['output'] for r in reports})!=1:
        raise ValueError('cross-architecture text results differ')
    manifest={**current,'status':'passed','scope':'headless-text-session-only','physical_hardware':False,
              'reports':{mode:digest((OUT/mode/'result.json').read_bytes()) for mode in ('native','arm','sanitize')}}
    (OUT/'verification.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('TEXT_SESSION_VERIFIED native arm sanitizer; keyboard=false offline_ime=false physical=false')

def main() -> None:
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=['test','matrix','verify'])
    parser.add_argument('--mode',choices=['native','arm','sanitize'],default='native')
    args=parser.parse_args()
    if args.action=='matrix':matrix_tests()
    elif args.action=='verify':verify()
    else:test(args.mode)

if __name__=='__main__':
    try: main()
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as exc:
        print('TEXT_INPUT_FAILED:',exc,file=sys.stderr)
        sys.exit(1)
