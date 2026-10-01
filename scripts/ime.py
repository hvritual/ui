#!/usr/bin/env python3
"""Locked offline-provider/TextSession/layout tests. Not rendered IME admission."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/ime'
LOCK = ROOT / 'toolchains/pinyin.lock.json'


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args: str) -> str:
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def state() -> dict:
    development = os.environ.get('IME_DEVELOPMENT') == '1'
    if not development and git('status', '--porcelain', '--untracked-files=all'):
        raise RuntimeError('clean committed source required')
    names = git('ls-files', '--cached', '--others', '--exclude-standard').splitlines()
    return {'commit': git('rev-parse', 'HEAD'), 'tree': git('write-tree'),
            'source_sha256': {n: sha(ROOT/n) for n in sorted(set(names)) if (ROOT/n).is_file()},
            'development': development}


def run(args: list, log: Path, expected: int = 0, timeout: int = 120, cwd: Path = ROOT) -> str:
    args = list(map(str, args))
    log.parent.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:abort_on_error=1',
           'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
    result = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    log.write_text('$ ' + ' '.join(args) + '\n' + result.stdout + f'\nexit={result.returncode}\n')
    if result.returncode != expected:
        print(result.stdout, file=sys.stderr)
        raise RuntimeError(f'exit {result.returncode}, expected {expected}: {log}')
    return result.stdout


def verify_files(root: Path, hashes: dict) -> None:
    if not root.is_dir() or root.is_symlink():
        raise RuntimeError('dependency directory missing or symlink')
    actual = {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()}
    if actual != set(hashes):
        raise RuntimeError('unlocked or missing dependency members')
    for name, digest in hashes.items():
        p = root / name
        if p.is_symlink() or not p.resolve().is_relative_to(root.resolve()) or sha(p) != digest:
            raise RuntimeError('dependency integrity: ' + name)


def prepare() -> Path:
    lock = json.loads(LOCK.read_text())
    original = Path(os.environ.get('PINYIN_SOURCE', OUT/'checkout'/lock['subdirectory']))
    verify_files(original, lock['source_sha256'])
    patch = ROOT/'patches/pinyin-no-user-dictionary.patch'
    if sha(patch) != lock['no_user_dictionary_patch_sha256']:
        raise RuntimeError('unreviewed engine patch')
    vendor = OUT/'vendor'
    if vendor.exists():
        verify_files(vendor, lock['prepared_sha256'])
    else:
        temporary = OUT/'preparing'
        if temporary.exists():
            raise RuntimeError('prior failed preparation must be inspected before removing it')
        shutil.copytree(original, temporary)
        for name, strip in [('0005-Fix-string-cast.patch', 6),
                            ('0004-Bundle-pinyin-dictionary-in-the-plugin.patch', 5)]:
            run(['patch', '--batch', '--fuzz=0', f'-p{strip}', '-R', '-i', temporary/'patches'/name],
                OUT/(name+'.log'), cwd=temporary)
        run(['patch', '--batch', '--fuzz=0', '-p1', '-i', patch], OUT/'no-user-dictionary.log', cwd=temporary)
        verify_files(temporary, lock['prepared_sha256'])
        temporary.rename(vendor)
    # Keep the unmodified source and attribution for independent reproducibility.
    archive = OUT/'upstream'
    if archive.exists():
        verify_files(archive, lock['source_sha256'])
    else:
        shutil.copytree(original, archive)
    write(OUT/'dependency.json', {'commit': lock['commit'], 'lock_sha256': sha(LOCK),
          'source_sha256': lock['source_sha256'], 'prepared_sha256': lock['prepared_sha256'],
          'patch_sha256': sha(patch), 'qt_linked': False, 'runtime_network': False})
    return vendor


def test(mode: str) -> None:
    before = state()
    vendor = prepare()
    import text_input
    unicode_source = text_input.dependencies()
    output = OUT/mode
    output.mkdir(parents=True, exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    arm = mode == 'arm'
    sanitize = mode == 'sanitize'
    cc, cxx = ('arm-linux-gnueabihf-gcc', 'arm-linux-gnueabihf-g++') if arm else ('gcc', 'g++')
    flags = ['-O1' if sanitize else '-O2', '-fno-strict-aliasing']
    if arm:
        flags += ['-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard']
    if sanitize:
        flags += ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    includes = ['-I.', '-I'+str(vendor/'include'), '-I'+str(unicode_source), '-DUTF8PROC_STATIC']
    sources = [(cxx, ROOT/'hosts/linux/ime/pinyin.cpp'), (cc, ROOT/'hosts/linux/ime/session.c'),
               (cc, ROOT/'hosts/linux/text-input/layout.c'), (cc, ROOT/'hosts/linux/text-input/session.c'),
               (cc, ROOT/'hosts/linux/package/package.c'), (cc, unicode_source/'utf8proc.c')]
    engine_sources = [(cxx, p) for p in sorted((vendor/'share').glob('*.cpp'))]
    objects, engine_objects = [], []
    for index, (compiler, path) in enumerate(sources+engine_sources):
        obj = output/f'{index}.o'
        strict = ['-Wall', '-Wextra', '-Werror', '-Wpedantic'] if path.is_relative_to(ROOT/'hosts') else []
        run([compiler, '-std=c11' if compiler==cc else '-std=c++11', *flags, *strict, *includes,
             '-c', path, '-o', obj], output/f'compile-{index}.log')
        objects.append(obj)
        if index >= len(sources):
            engine_objects.append(obj)
    binary = output/'ime-test'
    run([cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', *flags, *includes,
         ROOT/'tests/ime/test_input.cpp', *objects, '-lpthread', *(['-static'] if arm else []),
         '-o', binary], output/'link.log')
    probe = output/'pinyin-probe'
    run([cxx, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', *flags, *includes,
         ROOT/'tests/ime/upstream_probe.cpp', *engine_objects, '-lpthread', *(['-static'] if arm else []),
         '-o', probe], output/'probe-link.log')
    runner = [os.environ.get('QEMU_ARM', 'qemu-arm'), '-cpu', 'cortex-a7'] if arm else []
    dictionary = vendor/'data/dict_pinyin.dat'
    observed = []
    for name in ('test', 'repeat', 'negative'):
        text = run([*runner, binary, dictionary, *(['--intentional-failure'] if name=='negative' else [])],
                   output/(name+'.log'), 1 if name=='negative' else 0, timeout=60)
        if name=='negative':
            if 'INTENTIONAL_IME_ASSERTION_FAILURE' not in text or 'IME_CORE_OK' in text:
                raise RuntimeError('intentional negative gate failed open')
        else:
            if 'IME_CORE_OK groups=9 rendered_ui=false physical=false' not in text:
                raise RuntimeError('incomplete real provider/session suite')
            observed.append(text)
    if observed[0] != observed[1]:
        raise RuntimeError('nondeterministic synthetic test results')
    probe_output = run([*runner, probe, dictionary], output/'probe.log', timeout=30)
    if 'PINYIN_STANDALONE_OK ui_integrated=false physical=false' not in probe_output:
        raise RuntimeError('standalone engine test incomplete')
    attributes = run(['readelf', '-h', '-l', '-d', binary], output/'elf.log')
    symbols = run(['nm', '-C', binary], output/'symbols.log')
    if any(s in symbols for s in ('QFile', 'QString', 'QMutex')):
        raise RuntimeError('unexpected Qt linkage')
    if arm and ('INTERP' in attributes or '(NEEDED)' in attributes or 'hard-float ABI' not in attributes):
        raise RuntimeError('static ARM ABI mismatch')
    if before != state():
        raise RuntimeError('project source changed during test')
    write(output/'result.json', {**before, 'mode': mode, 'scope': 'offline-provider-text-session-layout-core',
          'qt_linked': False, 'rendered_ui': False, 'physical_hardware': False,
          'production_admission': False, 'runtime_package_integration': False,
          'instrumented_engine_and_unicode': sanitize,
          'binary_sha256': sha(binary), 'probe_sha256': sha(probe),
          'dependency_sha256': sha(OUT/'dependency.json'),
          'unicode_dependency_sha256': {n:sha(unicode_source/n) for n in ('utf8proc.c','utf8proc.h','utf8proc_data.c','LICENSE.md')},
          'normalized_output': observed[0], 'probe_output': probe_output,
          'logs': {p.name:sha(p) for p in sorted(output.glob('*.log'))}})
    print('OFFLINE_IME_CORE_TEST_OK', mode)


def verify() -> None:
    current = state()
    reports = []
    for mode in ('native','arm','sanitize'):
        directory = OUT/mode
        r = json.loads((directory/'result.json').read_text())
        if any(r[k]!=current[k] for k in current):
            raise RuntimeError('stale source or scope')
        if any(r[k] for k in ('qt_linked','rendered_ui','physical_hardware','production_admission','runtime_package_integration')):
            raise RuntimeError('scope overclaim')
        if r['binary_sha256']!=sha(directory/'ime-test') or r['probe_sha256']!=sha(directory/'pinyin-probe'):
            raise RuntimeError('binary changed')
        if r['dependency_sha256']!=sha(OUT/'dependency.json'):
            raise RuntimeError('dependency changed')
        for name,digest in r['logs'].items():
            if sha(directory/name)!=digest:
                raise RuntimeError('raw log changed')
        reports.append(r)
    for r in reports[1:]:
        for key in ('normalized_output','probe_output','unicode_dependency_sha256'):
            if r[key]!=reports[0][key]:
                raise RuntimeError('cross-architecture mismatch: '+key)
    write(OUT/'verification.json', {**current, 'status':'passed', 'scope':reports[0]['scope'],
          'rendered_ui':False,'physical_hardware':False,'runtime_package_integration':False,
          'reports':{r['mode']:sha(OUT/r['mode']/'result.json') for r in reports}})
    run(['git','archive','--format=tar','-o',OUT/'source.tar','HEAD'],OUT/'source-archive.log')
    print('OFFLINE_IME_CORE_VERIFIED not-rendered not-production')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('action', choices=['prepare','test','verify'])
    parser.add_argument('--mode',choices=['native','arm','sanitize'],default='native')
    args=parser.parse_args()
    try:
        if args.action=='prepare': prepare()
        elif args.action=='test': test(args.mode)
        else: verify()
    except Exception as exc:
        print('OFFLINE_IME_FAILED:',exc,file=sys.stderr)
        sys.exit(1)
