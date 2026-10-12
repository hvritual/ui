#!/usr/bin/env python3
"""Guarded Service IPC builds and real socket tests; never device acceptance."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from runtime_boundary import validate_command

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/ipc'
CORE = ['hosts/linux/ipc/' + n + '.c' for n in ('wire', 'client', 'broker')]
TARGETS = {
    'pocket-ipcd': (['hosts/linux/ipc/wire.c', 'hosts/linux/ipc/broker.c',
                     'hosts/linux/ipc/broker_main.c'], 'runtime'),
    'service-fixture': (['hosts/linux/ipc/wire.c', 'hosts/linux/ipc/service.c',
                         'tests/ipc/service_fixture.c'], 'runtime-test'),
    'test-core': ([*CORE, 'tests/ipc/test_core.c'], 'runtime-test'),
    'test-service': ([*CORE, 'hosts/linux/ipc/service.c', 'tests/ipc/test_service.c'], 'runtime-test'),
}
MARKERS = {'test-core.log': 'IPC_CORE_OK', 'test-service.log': 'IPC_SERVICE_OK',
           'sdk-unit.log': 'IPC_JS_SDK_OK'}


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


def runner(mode: str, static: bool = False) -> list[str]:
    return [] if mode == 'native' else ['qemu-arm', '-cpu', 'cortex-a7',
                                      *([] if static else ['-L', '/usr/arm-linux-gnueabihf'])]


def check_static_arm_abi(text: str, label: str) -> None:
    required = (r'^\s*Class:\s+ELF32\s*$', r'^\s*Machine:\s+ARM\s*$',
                r'^\s*Data:.*little endian\s*$', r'^\s*Flags:.*Version5 EABI.*hard-float ABI\s*$',
                r'^\s*Tag_CPU_arch:\s+v7\s*$', r'^\s*Tag_ABI_VFP_args:\s+VFP registers\s*$')
    if any(re.search(pattern, text, re.MULTILINE) is None for pattern in required) or \
            re.search(r'\b(?:INTERP|NEEDED)\b', text):
        raise RuntimeError('ARMv7 static hard-float ABI not demonstrated: ' + label)


def build(mode: str, sanitize: bool = False, development: bool = False,
          static: bool = False) -> dict[str, Path]:
    if sanitize and mode != 'native':
        raise RuntimeError('sanitizer is a native software gate')
    if static and (mode != 'arm' or sanitize):
        raise RuntimeError('static IPC builds require unsanitized ARM mode')
    before = source(development)
    cc = 'gcc' if mode == 'native' else 'arm-linux-gnueabihf-gcc'
    version = subprocess.check_output([cc, '-dumpfullversion'], text=True).strip()
    locked = json.loads((ROOT / 'toolchains/sources.lock.json').read_text())['toolchain']['gcc_major']
    if not development and int(version.split('.')[0]) != locked:
        raise RuntimeError('IPC acceptance requires locked GCC major ' + str(locked))
    directory = OUT / ('arm-static' if static else 'sanitize' if sanitize else mode)
    directory.mkdir(parents=True, exist_ok=True)
    binaries = {}
    for name, (sources, consumer) in TARGETS.items():
        binary = directory / name
        options = [] if mode == 'native' else ['-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard']
        if sanitize:
            options += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
        command = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                   *options, *(['-static'] if static else []), '-I.', *sources, '-o', str(binary)]
        ownership = validate_command(command, ROOT, consumer)
        run(command, directory / (name + '-build.log'))
        ownership.update(source_commit=before['commit'], source_tree=before['tree'],
                         development=development, binary_sha256=sha(binary))
        write(binary.with_suffix('.ownership.json'), ownership)
        binaries[name] = binary
    static_abis = {}
    if static:
        for name, binary in binaries.items():
            log = directory / (name + '-elf.log')
            abi = run(['arm-linux-gnueabihf-readelf', '-h', '-A', '-l', '-d', binary], log)
            check_static_arm_abi(abi, name)
            static_abis[log.name] = sha(log)
        # Retain the ordinary broker ABI field for consumers of build.json;
        # the static record additionally binds every executable's own ABI log.
        (directory / 'elf.log').write_bytes((directory / 'pocket-ipcd-elf.log').read_bytes())
        abi = (directory / 'elf.log').read_text()
    else:
        abi = run(['readelf', '-h', '-A', '-d', binaries['pocket-ipcd']], directory / 'elf.log')
    if mode == 'arm' and ('Machine:                           ARM' not in abi or
                          'Tag_ABI_VFP_args: VFP registers' not in abi):
        raise RuntimeError('IPC broker ARM hard-float ABI not demonstrated')
    if before != source(development):
        raise RuntimeError('sources changed during IPC build')
    write(directory / 'build.json', {**before, 'mode': mode, 'sanitizer': sanitize, 'gcc': version,
          'binaries': {n: sha(p) for n, p in binaries.items()}, 'abi_sha256': sha(directory / 'elf.log'),
          'ownership': {n: sha(p.with_suffix('.ownership.json')) for n, p in binaries.items()},
          'logs': {n + '-build.log': sha(directory / (n + '-build.log')) for n in binaries},
          **({'static': True, 'abi_logs': static_abis} if static else {})})
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


def required_map(value: object, keys: set[str], label: str) -> dict:
    if not isinstance(value, dict) or set(value) != keys or any(
            not isinstance(v, str) or not re.fullmatch(r'[0-9a-f]{64}', v) for v in value.values()):
        raise RuntimeError('incomplete IPC evidence set: ' + label)
    return value


def check_log(path: Path, digest: str, marker: str | None = None) -> None:
    if sha(path) != digest:
        raise RuntimeError('IPC log evidence drift: ' + str(path))
    text = path.read_text()
    if not text.startswith('$ ') or not text.endswith('\nexit=0\n'):
        raise RuntimeError('IPC successful process exit not recorded: ' + str(path))
    if marker and not any(line == marker or line.startswith(marker + ' ') for line in text.splitlines()):
        raise RuntimeError('IPC completion marker missing: ' + str(path))


def verify_record(mode: str, current: dict) -> str:
    directory = OUT / mode
    record = json.loads((directory / 'test.json').read_text())
    actual_mode, sanitized = ('native' if mode == 'sanitize' else mode), mode == 'sanitize'
    if any(record.get(k) != v for k, v in current.items()) or record.get('physical_hardware') is not False:
        raise RuntimeError('stale or noncanonical IPC evidence: ' + mode)
    if record.get('mode') != actual_mode or record.get('sanitizer') is not sanitized or record.get('scope') != 'real-socket-software-functional':
        raise RuntimeError('wrong IPC test mode or scope')
    binaries = required_map(record.get('binaries'), set(TARGETS), 'test binaries')
    expected_logs = {'test-core.log', 'test-service.log'} | ({'sdk-unit.log'} if mode == 'native' else set())
    logs = required_map(record.get('logs'), expected_logs, 'test logs')
    relative = record.get('run_dir')
    if not isinstance(relative, str) or not re.fullmatch(mode + r'/tests-[0-9]+', relative):
        raise RuntimeError('invalid IPC evidence directory')
    run_dir = OUT / relative
    if not run_dir.is_dir() or run_dir.resolve().parent != directory.resolve():
        raise RuntimeError('missing or escaped IPC evidence directory')
    build = json.loads((directory / 'build.json').read_text())
    if record.get('build_sha256') != sha(directory / 'build.json'):
        raise RuntimeError('IPC build evidence drift')
    if any(build.get(k) != v for k, v in current.items()) or build.get('mode') != actual_mode or build.get('sanitizer') is not sanitized:
        raise RuntimeError('stale or wrong IPC build source/mode')
    locked = json.loads((ROOT / 'toolchains/sources.lock.json').read_text())['toolchain']['gcc_major']
    if not isinstance(build.get('gcc'), str) or not re.fullmatch(str(locked) + r'\.[0-9]+(?:\.[0-9]+)?', build['gcc']):
        raise RuntimeError('IPC compiler baseline not demonstrated')
    if required_map(build.get('binaries'), set(TARGETS), 'build binaries') != binaries:
        raise RuntimeError('IPC build/test binary identity differs')
    ownerships = required_map(build.get('ownership'), set(TARGETS), 'ownership files')
    build_logs = required_map(build.get('logs'), {n + '-build.log' for n in TARGETS}, 'build logs')
    abi = directory / 'elf.log'
    check_log(abi, build.get('abi_sha256', ''))
    if actual_mode == 'arm' and ('Machine:                           ARM' not in abi.read_text() or
                                 'Tag_ABI_VFP_args: VFP registers' not in abi.read_text()):
        raise RuntimeError('IPC ARM ABI evidence incomplete')
    policy_sha = sha(ROOT / 'contracts/runtime-ownership.json')
    for name, (inputs, consumer) in TARGETS.items():
        binary = directory / name
        ownership_path = binary.with_suffix('.ownership.json')
        ownership = json.loads(ownership_path.read_text())
        if sha(binary) != binaries[name] or ownership.get('binary_sha256') != binaries[name] or sha(ownership_path) != ownerships[name]:
            raise RuntimeError('IPC binary/ownership evidence drift')
        if ownership.get('source_commit') != current['commit'] or ownership.get('source_tree') != current['tree'] or ownership.get('development') is not False:
            raise RuntimeError('IPC ownership source identity differs')
        if ownership.get('consumer') != consumer or ownership.get('runtime_input_policy_passed') is not True:
            raise RuntimeError('incorrect IPC build consumer')
        if ownership.get('ownership_manifest_sha256') != policy_sha or set(ownership.get('inputs', {})) != set(inputs):
            raise RuntimeError('IPC ownership manifest/input set differs')
        hashes = ownership.get('sha256')
        if not isinstance(hashes, dict) or not set(inputs) <= set(hashes):
            raise RuntimeError('IPC dependency hashes incomplete')
        for path, digest in hashes.items():
            resolved = (ROOT / path).resolve()
            if Path(path).is_absolute() or not resolved.is_relative_to(ROOT.resolve()) or sha(resolved) != digest:
                raise RuntimeError('IPC source/dependency evidence drift')
        check_log(directory / (name + '-build.log'), build_logs[name + '-build.log'])
    for name, digest in logs.items():
        check_log(run_dir / name, digest, MARKERS[name])
    return sha(directory / 'test.json')


def verify() -> None:
    (OUT / 'verification.json').unlink(missing_ok=True)
    current = source(False)
    records = {mode: verify_record(mode, current) for mode in ('native', 'arm', 'sanitize')}
    write(OUT / 'verification.json', {**current, 'status': 'passed', 'tests': records,
          'scope': 'real-socket-software-functional', 'physical_hardware': False,
          'production_authentication': False, 'application_vm_proof': 'separate ipc_acceptance.py gate'})
    print('IPC_VERIFY_OK', flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'test', 'verify'))
    parser.add_argument('--mode', choices=('native', 'arm'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--static', action='store_true',
                        help='ARM build only, in a separate arm-static directory; no device admission')
    parser.add_argument('--development', action='store_true',
                        help='diagnostic build only; cannot satisfy verify or CI acceptance')
    args = parser.parse_args()
    if args.static and args.action != 'build':
        raise RuntimeError('--static is a build option; board package tests have a separate gate')
    if args.action == 'verify':
        if args.development:
            raise RuntimeError('development evidence cannot be verified as acceptance')
        verify()
    elif args.action == 'build':
        build(args.mode, args.sanitize, args.development, static=args.static)
    else:
        test(args.mode, args.sanitize, args.development)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('IPC_FAILED:', exc, file=sys.stderr)
        sys.exit(1)
