#!/usr/bin/env python3
"""Guarded Service IPC builds and real socket tests; never device acceptance."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from runtime_boundary import validate_command

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/ipc'
CORE = ['hosts/linux/ipc/' + n + '.c' for n in ('wire', 'client', 'broker')]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def git(*args: str) -> str:
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def source(development: bool = False) -> dict:
    if not development and git('status', '--porcelain', '--untracked-files=all'):
        raise RuntimeError('IPC acceptance requires a committed clean checkout')
    names = git('ls-files', '--cached', '--others', '--exclude-standard').splitlines()
    return {'commit': git('rev-parse', 'HEAD'), 'tree': git('rev-parse', 'HEAD^{tree}'),
            'development': development,
            'source_files': {n: sha(ROOT / n) for n in sorted(set(names)) if (ROOT / n).is_file()}}


def run(command: list, log: Path, timeout: int = 120) -> str:
    command = list(map(str, command))
    print('+', ' '.join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text('$ ' + ' '.join(command) + '\n' + result.stdout + '\nexit=' + str(result.returncode) + '\n')
    print(result.stdout, end='', flush=True)
    if result.returncode:
        raise RuntimeError('command failed: ' + str(log))
    return result.stdout


def runner(mode: str) -> list[str]:
    return [] if mode == 'native' else ['qemu-arm', '-cpu', 'cortex-a7', '-L', '/usr/arm-linux-gnueabihf']


def build(mode: str, sanitize: bool = False, development: bool = False) -> dict[str, Path]:
    if sanitize and mode != 'native':
        raise RuntimeError('sanitizer is a native software gate')
    before = source(development)
    cc = 'gcc' if mode == 'native' else 'arm-linux-gnueabihf-gcc'
    version = subprocess.check_output([cc, '-dumpfullversion'], text=True).strip()
    locked = json.loads((ROOT / 'toolchains/sources.lock.json').read_text())['toolchain']['gcc_major']
    if not development and int(version.split('.')[0]) != locked:
        raise RuntimeError('IPC acceptance requires locked GCC major ' + str(locked))
    directory = OUT / ('sanitize' if sanitize else mode)
    directory.mkdir(parents=True, exist_ok=True)
    targets = {
        'pocket-ipcd': (['hosts/linux/ipc/wire.c', 'hosts/linux/ipc/broker.c',
                         'hosts/linux/ipc/broker_main.c'], 'runtime'),
        'service-fixture': (['hosts/linux/ipc/wire.c', 'hosts/linux/ipc/service.c',
                             'tests/ipc/service_fixture.c'], 'runtime-test'),
        'test-core': ([*CORE, 'tests/ipc/test_core.c'], 'runtime-test'),
        'test-service': ([*CORE, 'hosts/linux/ipc/service.c', 'tests/ipc/test_service.c'], 'runtime-test'),
    }
    binaries = {}
    for name, (sources, consumer) in targets.items():
        binary = directory / name
        options = [] if mode == 'native' else ['-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard']
        if sanitize:
            options += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
        command = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                   *options, '-I.', *sources, '-o', str(binary)]
        ownership = validate_command(command, ROOT, consumer)
        run(command, directory / (name + '-build.log'))
        ownership.update(source_commit=before['commit'], source_tree=before['tree'],
                         development=development, binary_sha256=sha(binary))
        write(binary.with_suffix('.ownership.json'), ownership)
        binaries[name] = binary
    abi = run(['readelf', '-h', '-A', '-d', binaries['pocket-ipcd']], directory / 'elf.log')
    if mode == 'arm' and ('Machine:                           ARM' not in abi or
                          'Tag_ABI_VFP_args: VFP registers' not in abi):
        raise RuntimeError('IPC broker ARM hard-float ABI not demonstrated')
    if before != source(development):
        raise RuntimeError('sources changed during IPC build')
    write(directory / 'build.json', {**before, 'mode': mode, 'sanitizer': sanitize, 'gcc': version,
          'binaries': {n: sha(p) for n, p in binaries.items()}, 'abi_sha256': sha(directory / 'elf.log')})
    return binaries


def test(mode: str, sanitize: bool = False, development: bool = False) -> None:
    before = source(development)
    binaries = build(mode, sanitize, development)
    directory = binaries['pocket-ipcd'].parent
    run_dir = directory / ('tests-' + str(time.time_ns()))
    for name, marker in (('test-core', 'IPC_CORE_OK'), ('test-service', 'IPC_SERVICE_OK')):
        output = run([*runner(mode), binaries[name]], run_dir / (name + '.log'))
        if marker not in output:
            raise RuntimeError('missing test completion: ' + name)
    # This is only a JS facade unit test. Actual QuickJS/native/renderer proof is
    # collected separately by ipc_acceptance.py using the production UI ELF.
    if mode == 'native' and not sanitize:
        output = run(['node', 'tests/ipc/sdk.js'], run_dir / 'sdk-unit.log')
        if 'IPC_JS_SDK_OK' not in output:
            raise RuntimeError('missing SDK unit completion')
    if before != source(development):
        raise RuntimeError('sources changed during IPC tests')
    write(directory / 'test.json', {**before, 'mode': mode, 'sanitizer': sanitize,
          'physical_hardware': False, 'scope': 'real-socket-software-functional',
          'run_dir': str(run_dir.relative_to(OUT)),
          'binaries': {n: sha(p) for n, p in binaries.items()},
          'logs': {p.name: sha(p) for p in sorted(run_dir.glob('*.log'))},
          'build_sha256': sha(directory / 'build.json')})
    print('IPC_TEST_OK', mode, 'development=' + str(development), flush=True)


def verify() -> None:
    current = source(False)
    records = {}
    for mode in ('native', 'arm', 'sanitize'):
        directory = OUT / mode
        record = json.loads((directory / 'test.json').read_text())
        if any(record.get(k) != v for k, v in current.items()) or record.get('physical_hardware') is not False:
            raise RuntimeError('stale or noncanonical IPC evidence: ' + mode)
        if record['sanitizer'] != (mode == 'sanitize'):
            raise RuntimeError('wrong IPC sanitizer scope')
        if record['build_sha256'] != sha(directory / 'build.json'):
            raise RuntimeError('IPC build evidence drift')
        for name, digest in record['binaries'].items():
            binary = directory / name
            ownership = json.loads(binary.with_suffix('.ownership.json').read_text())
            if sha(binary) != digest or ownership['binary_sha256'] != digest or ownership['source_commit'] != current['commit']:
                raise RuntimeError('IPC binary/ownership evidence drift')
            if name == 'pocket-ipcd' and ownership['consumer'] != 'runtime':
                raise RuntimeError('test broker substituted for production')
        for name, digest in record['logs'].items():
            if sha(OUT / record['run_dir'] / name) != digest:
                raise RuntimeError('IPC test evidence drift')
        records[mode] = sha(directory / 'test.json')
    write(OUT / 'verification.json', {**current, 'status': 'passed', 'tests': records,
          'scope': 'real-socket-software-functional', 'physical_hardware': False,
          'production_authentication': False, 'application_vm_proof': 'separate ipc_acceptance.py gate'})
    print('IPC_VERIFY_OK', flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'test', 'verify'))
    parser.add_argument('--mode', choices=('native', 'arm'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--development', action='store_true',
                        help='diagnostic build only; cannot satisfy verify or CI acceptance')
    args = parser.parse_args()
    if args.action == 'verify':
        if args.development:
            raise RuntimeError('development evidence cannot be verified as acceptance')
        verify()
    elif args.action == 'build':
        build(args.mode, args.sanitize, args.development)
    else:
        test(args.mode, args.sanitize, args.development)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('IPC_FAILED:', exc, file=sys.stderr)
        sys.exit(1)
