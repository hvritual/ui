#!/usr/bin/env python3
"""Verify collected headless IPC evidence with only the Python standard library.

This checks evidence consistency. Board mode is an operator report, never an
automatic assertion of physical hardware, display, or machine-service admission.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import stat
import struct
import sys

PROFILES = ['imx6ul-1024x600', 'imx6ul-1024x800']
BINARY_NAMES = ('ui-framework', 'pocket-ipcd', 'service-fixture')
SCOPE = 'headless-synthetic-counter-diagnostic'
CAPABILITIES = {'ui.core': 1, 'ipc.query': 16, 'ipc.command': 32,
                'ipc.events': 64, 'ipc.state': 128}
CASES = {
    **{name + '-' + str(height): (height, 'ipc-' + name, 3)
       for name in ('example', 'vendor') for height in (600, 800)},
    **{'denied-' + channel: (600, 'denied-' + channel, 10)
       for channel in ('query', 'command', 'events', 'state')},
    'offline': (600, 'ipc-offline', 10),
}
PACKAGE_NAMES = {name + '.pui' for _, name, _ in CASES.values()}
SOURCE_COPIES = {'run-ipc.sh': 'scripts/device/run-ipc.sh',
                 'verify-ipc-evidence.py': 'scripts/ipc_board_verify.py',
                 'README.md': 'docs/ipc-board.md'}
EXPECTED_INPUTS = {
    'pocket-ipcd': {'hosts/linux/ipc/wire.c', 'hosts/linux/ipc/broker.c',
                   'hosts/linux/ipc/broker_main.c'},
    'service-fixture': {'hosts/linux/ipc/wire.c', 'hosts/linux/ipc/service.c',
                        'tests/ipc/service_fixture.c'},
}
UI_INPUTS = {'hosts/linux/application/application.c', 'hosts/linux/application/program.c',
             'hosts/linux/ipc/wire.c', 'hosts/linux/ipc/client.c', 'hosts/linux/package/loaded.c'}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha(path: Path) -> str:
    with path.open('rb') as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: Path) -> dict:
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate JSON key: ' + key)
            result[key] = value
        return result
    value = json.loads(path.read_text(), object_pairs_hook=unique_object)
    require(isinstance(value, dict), 'JSON object required: ' + str(path))
    return value


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def caps_for(package_name: str) -> int:
    missing = 'ipc.' + package_name.removeprefix('denied-')
    return sum(value for name, value in CAPABILITIES.items()
               if not package_name.startswith('denied-') or name != missing)


def case_contracts() -> dict:
    return {name: {'profile': 'imx6ul-1024x' + str(height),
                   'package': package_name + '.pui', 'selection': selection,
                   'capabilities': caps_for(package_name), 'heap_bytes': 2 * 1024 * 1024}
            for name, (height, package_name, selection) in CASES.items()}


def file_hashes(directory: Path, *, ignore_logs: bool = False) -> dict[str, str]:
    require(directory.is_dir() and not directory.is_symlink(), 'invalid evidence directory')
    result = {}
    for path in sorted(directory.rglob('*')):
        require(not path.is_symlink(), 'symlinks are not evidence: ' + str(path))
        relative = path.relative_to(directory).as_posix()
        if ignore_logs and (relative == 'logs' or relative.startswith('logs/')):
            continue
        if path.is_file():
            if relative != 'SHA256SUMS':
                result[relative] = sha(path)
        elif not path.is_dir():
            raise RuntimeError('nonregular evidence member: ' + str(path))
    return result


def checksum_entries(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        require(match is not None, 'invalid SHA256SUMS entry')
        digest, name = match.groups()
        relative = PurePosixPath(name)
        # BusyBox find may emit ./ paths. Both spellings identify one member.
        require(not relative.is_absolute() and '..' not in relative.parts and
                '\\' not in name and relative.as_posix() not in ('', '.', 'SHA256SUMS'),
                'unsafe SHA256SUMS member')
        name = relative.as_posix()
        require(name not in result, 'duplicate SHA256SUMS member')
        result[name] = digest
    require(bool(result), 'empty SHA256SUMS evidence')
    return result


def check_sums(directory: Path, required: set[str], *, ignore_logs: bool = False) -> dict:
    entries = checksum_entries(directory / 'SHA256SUMS')
    require(required <= set(entries), 'required evidence set incomplete')
    require(entries == file_hashes(directory, ignore_logs=ignore_logs), 'evidence files or hashes differ')
    return entries


def check_static_elf(path: Path) -> None:
    """Check the shipped bytes, in addition to their captured readelf report."""
    data = path.read_bytes()
    require(len(data) >= 52 and data[:7] == b'\x7fELF\x01\x01\x01', 'static ARM ELF header invalid')
    kind, machine, version = struct.unpack_from('<HHI', data, 16)
    phoff = struct.unpack_from('<I', data, 28)[0]
    flags = struct.unpack_from('<I', data, 36)[0]
    ehsize, phsize, phcount = struct.unpack_from('<HHH', data, 40)
    require(kind == 2 and machine == 40 and version == 1 and ehsize == 52 and
            flags & 0xff000000 == 0x05000000 and flags & 0x400 and not flags & 0x200,
            'static ARM EABI/hard-float identity invalid')
    require(phsize == 32 and phcount > 0 and phoff >= 52 and
            phoff + phcount * phsize <= len(data), 'static ARM program headers invalid')
    loadable = False
    for offset in range(phoff, phoff + phcount * phsize, phsize):
        ptype, file_offset, _, _, file_size, _, _, _ = struct.unpack_from('<8I', data, offset)
        require(ptype not in (2, 3), 'dynamic loader/section in static ARM ELF')
        require(file_offset + file_size <= len(data), 'static ARM segment outside file')
        loadable |= ptype == 1
    require(loadable, 'static ARM load segment absent')


def check_log(path: Path, marker: str | None = None) -> str:
    text = path.read_text()
    require(text.startswith('$ ') and text.endswith('\nexit=0\n'),
            'successful process exit not recorded: ' + path.name)
    require(re.search(r'^RUNNER_(?:CASE_TIMEOUT|READINESS_TIMEOUT|FORCED_STOP|UNEXPECTED_EARLY_EXIT)\b',
                      text, re.MULTILINE) is None, 'recorded runner failure: ' + path.name)
    if marker:
        require(any(line == marker or line.startswith(marker + ' ') for line in text.splitlines()),
                'completion marker missing: ' + path.name)
    return text


def check_abi_log(path: Path) -> None:
    text = check_log(path)
    patterns = (r'^\s*Class:\s+ELF32\s*$', r'^\s*Machine:\s+ARM\s*$',
                r'^\s*Data:.*little endian\s*$', r'^\s*Flags:.*Version5 EABI.*hard-float ABI\s*$',
                r'^\s*Tag_CPU_arch:\s+v7\s*$', r'^\s*Tag_ABI_VFP_args:\s+VFP registers\s*$')
    require(all(re.search(pattern, text, re.MULTILINE) for pattern in patterns) and
            not re.search(r'\b(?:INTERP|NEEDED)\b', text), 'static ARM ABI log invalid')


def verify_bundle(directory: Path) -> dict:
    required = {'manifest.json', 'run-ipc.sh', 'README.md', 'verify-ipc-evidence.py',
                'evidence/source.json', 'evidence/runtime-ownership.json',
                'evidence/toolchain.json', *BINARY_NAMES}
    required |= {'packages/' + name for name in PACKAGE_NAMES}
    required |= {'evidence/' + name + suffix for name in BINARY_NAMES
                 for suffix in ('.ownership.json', '-build.log', '-elf.log')}
    check_sums(directory, required, ignore_logs=True)
    manifest = read_json(directory / 'manifest.json')
    expected = {'schema': 1, 'scope': SCOPE, 'profiles': PROFILES, 'static': True,
                'headless': True, 'physical_hardware': False, 'business_commands': False,
                'production_authentication': False, 'development': False}
    require(all(key in manifest and type(manifest[key]) is type(value) and manifest[key] == value
                for key, value in expected.items()), 'package mode/scope invalid')
    require(manifest.get('cases') == case_contracts(), 'package case matrix invalid')
    for field, names in (('binaries', set(BINARY_NAMES)), ('packages', PACKAGE_NAMES)):
        mapping = manifest.get(field)
        require(isinstance(mapping, dict) and set(mapping) == names and
                all(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value)
                    for value in mapping.values()), 'package identity set incomplete')
        for name, digest in mapping.items():
            require(sha(directory / ('packages' if field == 'packages' else '') / name) == digest,
                    'package binary/payload hash differs')
    source = read_json(directory / 'evidence/source.json')
    require(source.get('development') is False and
            source.get('commit') == manifest.get('source_commit') and
            source.get('tree') == manifest.get('source_tree') and
            all(isinstance(source.get(key), str) and re.fullmatch(r'[0-9a-f]{40}', source[key])
                for key in ('commit', 'tree')), 'package source identity invalid')
    hashes = source.get('source_files')
    require(isinstance(hashes, dict) and bool(hashes) and
            all(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value)
                for value in hashes.values()), 'package source hashes incomplete')
    for copied, original in SOURCE_COPIES.items():
        require(hashes.get(original) == sha(directory / copied), 'shipped script/document source drift')
    policy_path = directory / 'evidence/runtime-ownership.json'
    policy = read_json(policy_path)
    require(hashes.get('contracts/runtime-ownership.json') == sha(policy_path), 'ownership policy source drift')
    toolchain = read_json(directory / 'evidence/toolchain.json')
    require(toolchain.get('gcc_major') == 11 and
            isinstance(toolchain.get('gcc_version'), str) and
            re.fullmatch(r'11\.[0-9]+(?:\.[0-9]+)?', toolchain['gcc_version']) is not None,
            'locked compiler identity missing')
    for name in BINARY_NAMES:
        binary = directory / name
        require(binary.stat().st_mode & stat.S_IXUSR, 'shipped binary is not executable')
        check_static_elf(binary)
        ownership = read_json(directory / 'evidence' / (name + '.ownership.json'))
        consumer = 'runtime-test' if name == 'service-fixture' else 'runtime'
        require(ownership.get('consumer') == consumer and ownership.get('runtime_input_policy_passed') is True and
                ownership.get('development') is False and ownership.get('binary_sha256') == manifest['binaries'][name] and
                ownership.get('source_commit') == source['commit'] and ownership.get('source_tree') == source['tree'] and
                ownership.get('ownership_manifest_sha256') == sha(policy_path), 'shipped ownership identity invalid')
        inputs, dependencies = ownership.get('inputs'), ownership.get('sha256')
        require(isinstance(inputs, dict) and bool(inputs) and isinstance(dependencies, dict) and
                set(inputs) <= set(dependencies), 'shipped ownership dependency set incomplete')
        if name == 'ui-framework':
            require(UI_INPUTS <= set(inputs) and not
                    {'hosts/linux/ipc/broker.c', 'hosts/linux/ipc/service.c'} & set(inputs),
                    'production UI IPC boundary invalid')
        else:
            require(set(inputs) == EXPECTED_INPUTS[name], 'shipped IPC link input set invalid')
        for filename, entry in inputs.items():
            require(policy.get('link_inputs', {}).get(filename) == entry, 'shipped ownership classification drift')
            require(isinstance(entry, dict) and entry.get('reference_only') is False and
                    (name == 'service-fixture' or entry.get('role') not in ('test-only', 'application')),
                    'test/application code in production executable')
        for filename, digest in dependencies.items():
            require(isinstance(filename, str) and not PurePosixPath(filename).is_absolute() and
                    '..' not in PurePosixPath(filename).parts and isinstance(digest, str) and
                    re.fullmatch(r'[0-9a-f]{64}', digest) is not None, 'shipped dependency identity invalid')
            if filename in hashes:
                require(hashes[filename] == digest, 'shipped source/dependency hash differs')
        check_log(directory / 'evidence' / (name + '-build.log'))
        check_abi_log(directory / 'evidence' / (name + '-elf.log'))
    require((directory / 'run-ipc.sh').stat().st_mode & stat.S_IXUSR, 'shipped launcher is not executable')
    return manifest


def ppm_hash(path: Path, height: int) -> str:
    header = ('P6\n1024 ' + str(height) + '\n255\n').encode()
    with path.open('rb') as stream:
        require(stream.read(len(header)) == header and
                path.stat().st_size == len(header) + 1024 * height * 3, 'PPM magic/dimensions/size invalid')
    return sha(path)


def absolute_record_path(value: object) -> str:
    require(isinstance(value, str) and value.startswith('/') and len(value) <= 4096 and
            not any(ord(char) < 32 for char in value) and '..' not in PurePosixPath(value).parts,
            'recorded execution path invalid')
    return value.rstrip('/') or '/'


def process_pid(text: str, seen: set[int]) -> int:
    rows = [line for line in text.splitlines() if line.startswith('process_pid=')]
    require(len(rows) == 1 and re.fullmatch(r'process_pid=[1-9][0-9]*', rows[0]) is not None,
            'process PID record missing or invalid')
    pid = int(rows[0].split('=', 1)[1])
    require(pid not in seen, 'process IDs must be distinct positive integers')
    seen.add(pid)
    return pid


def command_options(text: str, prefix: list[str], *, flags: set[str] | None = None) -> dict:
    argv = shlex.split(text.splitlines()[0][2:])
    require(argv[:len(prefix)] == prefix, 'process executable/mode differs')
    options = {}
    index = len(prefix)
    flags = flags or set()
    while index < len(argv):
        key = argv[index]
        require(key.startswith('--') and key not in options, 'duplicate/invalid process option')
        index += 1
        if key in flags:
            options[key] = None
        else:
            require(index < len(argv), 'missing process option value')
            options[key] = argv[index]
            index += 1
    return options


def verify_run(package: Path, report: Path, *, expected_mode: str | None = None,
               write_result: bool = True) -> dict:
    require(report.is_dir() and not report.is_symlink(), 'invalid report directory')
    result_path = report / 'verification.json'
    result_path.unlink(missing_ok=True)
    manifest = verify_bundle(package)
    required = {'run.json', 'bundle-manifest.json', 'process_groups.tsv',
                'startup.log', 'environment.log', 'bundle-check.log'}
    required |= {name + '.log' for name in CASES}
    required |= {name + '/' + member for name in CASES
                 for member in ('report.json', 'first.ppm', 'last.ppm', 'timeline.csv')}
    required |= {role + '-' + str(height) + '.log' for role in ('broker', 'provider', 'vendor-provider')
                 for height in (600, 800)}
    evidence = check_sums(report, required)
    run = read_json(report / 'run.json')
    require(run.get('schema') == 1 and type(run.get('schema')) is int and
            run.get('mode') in ('qemu', 'board') and run.get('completed') is True and
            type(run.get('exit_code')) is int and run['exit_code'] == 0 and
            run.get('physical_io') is False and run.get('business_commands') is False and
            run.get('production_authentication') is False,
            'run mode/completion/scope invalid')
    mode = run['mode']
    require(expected_mode is None or expected_mode == mode, 'unexpected execution mode')
    emulator = absolute_record_path(run.get('emulator')) if mode == 'qemu' else None
    require(mode == 'qemu' or run.get('emulator') is None, 'board mode contains an emulator')
    recorded_package = absolute_record_path(run.get('package_directory'))
    recorded_output = absolute_record_path(run.get('output_directory'))
    prefix = [emulator, '-cpu', 'cortex-a7'] if emulator else []
    require(run.get('manifest_sha256') == sha(package / 'manifest.json') ==
            sha(report / 'bundle-manifest.json'), 'run/package manifest identity differs')
    startup = (report / 'startup.log').read_text().splitlines()
    sequence = ['example-600', 'vendor-600', 'denied-query', 'denied-command', 'denied-events',
                'denied-state', 'example-800', 'vendor-800', 'offline']
    require(startup.count('IPC_BOARD_DIAGNOSTIC_BEGIN mode=' + mode) == 1 and
            startup[-1:] == ['IPC_BOARD_COLLECTED mode=' + mode] and
            not any(line.startswith('IPC_BOARD_FAILED') for line in startup) and
            [line for line in startup if line.startswith('IPC_CASE_')] ==
            [marker + name for name in sequence for marker in ('IPC_CASE_BEGIN ', 'IPC_CASE_COLLECTED ')],
            'launcher case order/completion invalid')
    environment = (report / 'environment.log').read_text()
    require(environment.startswith('mode=' + mode + '\npackage_directory=' + recorded_package +
                                   '\noutput_directory=' + recorded_output + '\n'),
            'environment execution identity differs')
    uid_match = re.search(r'^uid=([0-9]+)(?:\(|\s)', environment, re.MULTILINE)
    require(uid_match is not None and '/proc/version\n' in environment and
            '/proc/cpuinfo\n' in environment and '/proc/meminfo\n' in environment,
            'environment CPU/kernel/memory/UID evidence incomplete')
    emulator_digest = None
    if mode == 'qemu':
        emulator_hashes = [match.group(1) for line in environment.splitlines()
                           if (match := re.fullmatch(r'\\?([0-9a-f]{64})  (.+)', line)) and
                           match.group(2) in (emulator, emulator.replace('\\', '\\\\'))]
        require('\nemulator=' + emulator + '\n' in environment and
                re.search(r'^qemu-arm version ', environment, re.MULTILINE) is not None and
                len(emulator_hashes) == 1,
                'emulator identity evidence incomplete')
        emulator_digest = emulator_hashes[0]
    else:
        require(re.search(r'\barmv7[^\s]*\b', environment) is not None and
                '\nemulator=' not in environment, 'reported board architecture/mode differs')
    checked = (report / 'bundle-check.log').read_text().splitlines()
    expected_checked = [name + ': OK' for name in checksum_entries(package / 'SHA256SUMS')]
    require(sorted(checked) == sorted(expected_checked), 'initial bundle checksum evidence incomplete')
    rows = (report / 'process_groups.tsv').read_text().splitlines()
    require(rows and rows[0] == 'profile\trole\tpid\tservice' and len(rows) == 7,
            'process group matrix incomplete')
    groups = {}
    for row in rows[1:]:
        columns = row.split('\t')
        require(len(columns) == 4, 'process group row invalid')
        profile, role, pid, service = columns
        require(profile in PROFILES and role in ('broker', 'provider', 'vendor-provider') and
                re.fullmatch(r'[1-9][0-9]*', pid) is not None and (profile, role) not in groups,
                'process group identity invalid')
        expected_service = {'broker': '-', 'provider': 'demo.counter.v1',
                            'vendor-provider': 'vendor.example.v1'}[role]
        require(service == expected_service, 'process group service differs')
        groups[profile, role] = int(pid)
    require(set(groups) == {(profile, role) for profile in PROFILES
                           for role in ('broker', 'provider', 'vendor-provider')},
            'process group matrix incomplete')
    seen: set[int] = set()
    sockets = {}
    for profile in PROFILES:
        broker_epochs = set()
        for role in ('broker', 'provider', 'vendor-provider'):
            text = check_log(report / (role + '-' + profile.rsplit('x', 1)[1] + '.log'))
            pid = process_pid(text, seen)
            require(pid == groups[profile, role], 'process group PID/log differs')
            binary = 'pocket-ipcd' if role == 'broker' else 'service-fixture'
            options = command_options(text, [*prefix, recorded_package.rstrip('/') + '/' + binary])
            socket = absolute_record_path(options.get('--socket'))
            if role == 'broker':
                require(set(options) == {'--socket', '--client-uid', '--service-uid'} and
                        re.fullmatch(r'[0-9]+', options['--client-uid']) is not None and
                        options['--client-uid'] == options['--service-uid'] == uid_match.group(1),
                        'broker UID configuration invalid')
                sockets[profile] = socket
            if role != 'broker':
                require(socket == sockets[profile], 'provider socket differs from its broker')
                service = 'demo.counter.v1' if role == 'provider' else 'vendor.example.v1'
                matches = re.findall(r'^SERVICE_READY service=' + re.escape(service) +
                                     r' epoch=([1-9][0-9]*) broker=([1-9][0-9]*) pid=' +
                                     str(pid) + r'$', text, re.MULTILINE)
                require(len(matches) == 1 and '\nSERVICE_STOPPED synthetic-counter-only;' in text,
                        'provider readiness/shutdown evidence missing')
                broker_epochs.add(matches[0][1])
                require(options.get('--service') == service, 'provider service argument differs')
                expected_options = {'--socket': socket, '--service': service}
                if role == 'vendor-provider':
                    expected_options.update({'--query': 'read', '--command': 'increase',
                                             '--event': 'updated', '--state': 'values'})
                require(options == expected_options, 'dynamic member/provider arguments differ')
        require(len(broker_epochs) == 1, 'dynamic provider used a different broker epoch')
    results = {}
    for name, (height, package_name, selection) in CASES.items():
        contract = case_contracts()[name]
        raw = read_json(report / name / 'report.json')
        payload = package / 'packages' / (package_name + '.pui')
        expected = {'schema': 1, 'commit': manifest['source_commit'], 'profile': contract['profile'],
                    'ok': True, 'error': None, 'completed': 1, 'application_selection': selection,
                    'package_admitted': True, 'package_sha256': manifest['packages'][payload.name],
                    'package_bytes': payload.stat().st_size, 'application_capabilities': contract['capabilities'],
                    'application_heap_limit': contract['heap_bytes'], 'ipc_development': True,
                    'ipc_command_capable': bool(contract['capabilities'] & 32),
                    'physical_io': False, 'visual_validated': False, 'business_commands': False,
                    'package_authenticated': False, 'core_live_bytes_after_close': 0, 'ipc_pending': 0}
        require(all(key in raw and type(raw[key]) is type(value) and raw[key] == value
                    for key, value in expected.items()) and raw.get('synthetic', False) is False,
                'raw Application report invalid: ' + name)
        require(all(type(raw.get(key)) is int and raw[key] >= 0
                    for key in ('ipc_requests', 'ipc_messages', 'ipc_failures', 'item_count', 'ticks', 'presents')),
                'raw Application counter invalid')
        require(raw['ticks'] > 0 and raw['presents'] > 0, 'rendered execution missing')
        if selection == 3:
            require(raw['item_count'] == 1 and raw['ipc_requests'] >= 9 and raw['ipc_messages'] >= 12 and
                    raw.get('ipc_ready') is True and type(raw.get('ipc_status')) is int and raw['ipc_status'] == 0,
                    'four channel traffic incomplete')
        else:
            require(raw['ipc_requests'] == raw['ipc_messages'] == 0 and raw['ipc_failures'] >= 1,
                    'denial/offline ordinary failure missing')
        text = check_log(report / (name + '.log'), 'FRAMEWORK_EXIT ok=1 error=none')
        process_pid(text, seen)
        options = command_options(text, [*prefix, recorded_package.rstrip('/') + '/ui-framework'],
                                  flags={'--headless', '--allow-unsigned-package'})
        socket = absolute_record_path(options.get('--ipc-development-socket'))
        require(options.get('--seconds') == ('8' if selection == 3 else '4'), 'UI observation interval differs')
        require(options == {'--headless': None, '--profile': contract['profile'],
                '--package': recorded_package.rstrip('/') + '/packages/' + payload.name,
                '--allow-unsigned-package': None, '--ipc-development-socket': socket,
                '--seconds': options['--seconds'], '--output': recorded_output.rstrip('/') + '/' + name},
                'UI case/package/mode command differs')
        require(socket == sockets[contract['profile']] if name != 'offline' else socket not in sockets.values(),
                'UI socket differs from its process group')
        if name == 'offline':
            require(all(PurePosixPath(socket) == PurePosixPath(peer_socket).parent / 'missing.sock'
                        for peer_socket in sockets.values()), 'offline socket is outside the private run directory')
        require('FRAMEWORK_FIRST_PRESENT profile=' + contract['profile'] + ' mode=headless commit=' +
                manifest['source_commit'] in text, 'UI first-present identity differs')
        first, last = ppm_hash(report / name / 'first.ppm', height), ppm_hash(report / name / 'last.ppm', height)
        require(first != last, 'IPC result did not change rendered pixels')
        require((report / name / 'timeline.csv').read_text().startswith('sample_ns,input_event_ns,'),
                'raw timeline missing')
        results[name] = {'profile': contract['profile'], 'package_sha256': expected['package_sha256'],
                         'selection': selection, 'completed': 1, 'ipc_requests': raw['ipc_requests'],
                         'ipc_messages': raw['ipc_messages'], 'first': first, 'last': last}
    result = {'schema': 1, 'status': 'passed', 'mode': mode,
              'scope': 'qemu-static-headless-software' if mode == 'qemu' else 'board-executed-headless-reported',
              'emulator_sha256': emulator_digest,
              'physical_hardware': False, 'physical_io': False, 'business_commands': False,
              'production_authentication': False, 'independent_board_review': 'pending',
              'source_commit': manifest['source_commit'], 'source_tree': manifest['source_tree'],
              'manifest_sha256': sha(package / 'manifest.json'), 'run_sha256': sha(report / 'run.json'),
              'checksums_sha256': sha(report / 'SHA256SUMS'), 'binaries': manifest['binaries'],
              'packages': manifest['packages'], 'results': results, 'evidence': evidence}
    if write_result:
        write_json(result_path, result)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True, help='unpacked IPC diagnostic package')
    parser.add_argument('--report', type=Path, required=True, help='complete returned run directory')
    args = parser.parse_args()
    result = verify_run(args.package, args.report)
    print('IPC_BOARD_EVIDENCE_OK mode=' + result['mode'] + ' scope=' + result['scope'] +
          ' physical_hardware=false independent_board_review=pending')


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('IPC_BOARD_EVIDENCE_FAILED:', exc, file=sys.stderr)
        sys.exit(1)
