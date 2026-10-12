"""Corrupt package/verifier fixtures; no fixture ELF or launcher is executed."""
from __future__ import annotations

import io
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import sys
import tarfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import ipc
import ipc_board as board
import ipc_board_verify as v
import test_framework_verification as framework_fixture
from runtime_boundary import read_policy

ABI = """Class: ELF32
Data: 2's complement, little endian
Machine: ARM
Flags: 0x5000400, Version5 EABI, hard-float ABI
Tag_CPU_arch: v7
Tag_ABI_VFP_args: VFP registers
"""


def unit_elf(name: str) -> bytes:
    """Syntactically inspectable bytes with no executable implementation."""
    data = bytearray(192)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHIIIIIHHHHHH', data, 16, 2, 40, 1, 0x10000, 52, 0,
                     0x5000400, 52, 32, 1, 40, 0, 0)
    struct.pack_into('<8I', data, 52, 1, 0, 0x10000, 0x10000, len(data), len(data), 5, 4096)
    marker = ('VERIFIER UNIT ONLY; NEVER EXECUTE: ' + name).encode()
    data[100:100 + len(marker)] = marker
    return bytes(data)


class BoardPackageTests(unittest.TestCase):
    def setUp(self):
        self.fixture = framework_fixture.FrameworkEvidenceTests('test_complete_verifier_fixture_structure')
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)
        self.temporary = Path(self.fixture.temporary.name)
        self.package = self.temporary / 'board/pocket-ipc-board'
        self.package.mkdir(parents=True)
        self.report = self.temporary / 'returned'
        self.report.mkdir()
        self.source = dict(self.fixture.current)
        self.source['source_files'] = {}
        self.metadata = self.package / 'evidence'
        self.metadata.mkdir()
        policy = read_policy()
        for name in v.BINARY_NAMES:
            binary = self.package / name
            binary.write_bytes(unit_elf(name))
            binary.chmod(0o755)
            inputs = v.UI_INPUTS if name == 'ui-framework' else v.EXPECTED_INPUTS[name]
            dependencies = {filename: v.sha(ROOT / filename) for filename in inputs}
            self.source['source_files'].update(dependencies)
            ownership = {'binary_sha256': v.sha(binary), 'source_commit': self.source['commit'],
                         'source_tree': self.source['tree'], 'development': False,
                         'consumer': 'runtime-test' if name == 'service-fixture' else 'runtime',
                         'runtime_input_policy_passed': True,
                         'ownership_manifest_sha256': v.sha(ROOT / 'contracts/runtime-ownership.json'),
                         'inputs': {filename: policy['link_inputs'][filename] for filename in inputs},
                         'sha256': dependencies}
            v.write_json(self.metadata / (name + '.ownership.json'), ownership)
            (self.metadata / (name + '-build.log')).write_text(
                '$ arm-linux-gnueabihf-gcc -static verifier-unit-only.c\nexit=0\n')
            (self.metadata / (name + '-elf.log')).write_text('$ readelf verifier-unit-only\n' + ABI + '\nexit=0\n')
        for copied, original in v.SOURCE_COPIES.items():
            (self.package / copied).write_text('VERIFIER UNIT ONLY; this file is never executed: ' + copied + '\n')
            self.source['source_files'][original] = v.sha(self.package / copied)
        (self.package / 'run-ipc.sh').chmod(0o755)
        shutil.copyfile(ROOT / 'contracts/runtime-ownership.json', self.metadata / 'runtime-ownership.json')
        self.source['source_files']['contracts/runtime-ownership.json'] = v.sha(self.metadata / 'runtime-ownership.json')
        v.write_json(self.metadata / 'source.json', self.source)
        v.write_json(self.metadata / 'toolchain.json', {'gcc_major': 11, 'gcc_version': '11.4.0'})
        (self.package / 'packages').mkdir()
        for name in v.PACKAGE_NAMES:
            shutil.copyfile(self.fixture.directory / name, self.package / 'packages' / name)
        self.manifest = {'schema': 1, 'source_commit': self.source['commit'], 'source_tree': self.source['tree'],
                         'scope': v.SCOPE, 'profiles': v.PROFILES, 'static': True, 'headless': True,
                         'physical_hardware': False, 'business_commands': False, 'production_authentication': False,
                         'development': False, 'cases': v.case_contracts(),
                         'binaries': {name: v.sha(self.package / name) for name in v.BINARY_NAMES},
                         'packages': {name: v.sha(self.package / 'packages' / name) for name in v.PACKAGE_NAMES},
                         'fixture': 'verifier-unit-only; no process execution'}
        self.recorded_package = '/board/unit space\'and"quote/pocket-ipc-board'
        self.recorded_output = '/board/returned space\'and"quote'
        self.emulator = '/unit/qemu-arm'
        self.run = {'schema': 1, 'mode': 'qemu', 'emulator': self.emulator,
                    'package_directory': self.recorded_package, 'output_directory': self.recorded_output,
                    'completed': True, 'exit_code': 0, 'physical_io': False,
                    'business_commands': False, 'production_authentication': False}
        for name in v.CASES:
            shutil.copytree(self.fixture.directory / name, self.report / name, copy_function=os.link)
        self.write_run_logs()
        self.seal()

    def command(self, binary, *args):
        prefix = [self.emulator, '-cpu', 'cortex-a7'] if self.run['mode'] == 'qemu' else []
        return '$ ' + shlex.join([*prefix, self.recorded_package + '/' + binary, *map(str, args)])

    def write_run_logs(self):
        socket = '/tmp/pocket-ipc.unit/broker.sock'
        groups = ['profile\trole\tpid\tservice']
        for height, start in ((600, 100), (800, 200)):
            profile = 'imx6ul-1024x' + str(height)
            for role, pid, service in (('broker', start, '-'), ('provider', start + 1, 'demo.counter.v1'),
                                       ('vendor-provider', start + 2, 'vendor.example.v1')):
                groups.append(profile + '\t' + role + '\t' + str(pid) + '\t' + service)
                args = ['--socket', socket]
                args += ['--client-uid', '1000', '--service-uid', '1000'] if role == 'broker' else ['--service', service]
                if role == 'vendor-provider':
                    args += ['--query', 'read', '--command', 'increase', '--event', 'updated', '--state', 'values']
                text = self.command('pocket-ipcd' if role == 'broker' else 'service-fixture', *args)
                text += '\nprocess_pid=' + str(pid) + '\n'
                if role == 'broker':
                    text += 'RUNNER_BROKER_READY socket=' + socket + '\n'
                else:
                    text += 'SERVICE_READY service=' + service + ' epoch=1 broker=' + str(height) + ' pid=' + str(pid) + '\n'
                    text += 'SERVICE_STOPPED synthetic-counter-only; no-device-admission; process-local-dedup\n'
                (self.report / (role + '-' + str(height) + '.log')).write_text(text + '\nexit=0\n')
        (self.report / 'process_groups.tsv').write_text('\n'.join(groups) + '\n')
        for index, (name, (height, package_name, selection)) in enumerate(v.CASES.items()):
            args = ['--headless', '--profile', 'imx6ul-1024x' + str(height),
                    '--package', self.recorded_package + '/packages/' + package_name + '.pui',
                    '--allow-unsigned-package', '--ipc-development-socket',
                    socket if name != 'offline' else '/tmp/pocket-ipc.unit/missing.sock',
                    '--seconds', '8' if selection == 3 else '4',
                    '--output', self.recorded_output + '/' + name]
            text = self.command('ui-framework', *args) + '\nprocess_pid=' + str(1000 + index) + '\n'
            text += 'FRAMEWORK_FIRST_PRESENT profile=imx6ul-1024x' + str(height) + ' mode=headless commit=' + self.source['commit'] + '\n'
            text += 'FRAMEWORK_EXIT ok=1 error=none output=' + self.recorded_output + '/' + name + '\nexit=0\n'
            (self.report / (name + '.log')).write_text(text)
        sequence = ['example-600', 'vendor-600', 'denied-query', 'denied-command', 'denied-events',
                    'denied-state', 'example-800', 'vendor-800', 'offline']
        startup = ['IPC_BOARD_DIAGNOSTIC_BEGIN mode=' + self.run['mode']]
        startup += [marker + name for name in sequence for marker in ('IPC_CASE_BEGIN ', 'IPC_CASE_COLLECTED ')]
        startup.append('IPC_BOARD_COLLECTED mode=' + self.run['mode'])
        (self.report / 'startup.log').write_text('\n'.join(startup) + '\n')
        environment = 'mode=' + self.run['mode'] + '\npackage_directory=' + self.recorded_package + '\noutput_directory=' + self.recorded_output + '\n'
        environment += 'Linux verifier-unit 0.0 armv7l unit-only\nuid=1000(unit) gid=1000(unit)\n'
        environment += '/proc/version\nverifier-only\n/proc/cpuinfo\nverifier-only\n/proc/meminfo\nverifier-only\n'
        if self.run['mode'] == 'qemu':
            environment += 'emulator=' + self.emulator + '\n' + 'a' * 64 + '  ' + self.emulator + '\nqemu-arm version 0.0 verifier-unit-only\n'
        (self.report / 'environment.log').write_text(environment)

    @staticmethod
    def checksums(directory):
        hashes = v.file_hashes(directory)
        hashes.pop('verification.json', None)
        (directory / 'SHA256SUMS').write_text(''.join(value + '  ' + name + '\n' for name, value in hashes.items()))

    def seal(self):
        v.write_json(self.package / 'manifest.json', self.manifest)
        self.checksums(self.package)
        (self.report / 'bundle-check.log').write_text(''.join(name + ': OK\n' for name in v.checksum_entries(self.package / 'SHA256SUMS')))
        shutil.copyfile(self.package / 'manifest.json', self.report / 'bundle-manifest.json')
        self.run['manifest_sha256'] = v.sha(self.package / 'manifest.json')
        v.write_json(self.report / 'run.json', self.run)
        self.checksums(self.report)

    def rejects(self, pattern, *, seal=True):
        if seal:
            self.seal()
        with self.assertRaisesRegex((RuntimeError, ValueError, OSError), pattern):
            v.verify_run(self.package, self.report, write_result=False)

    def raw(self, name='example-600', **changes):
        path = self.report / name / 'report.json'
        value = json.loads(path.read_text())
        value.update(changes)
        v.write_json(path, value)

    def test_complete_structure_and_quoted_paths(self):
        result = v.verify_run(self.package, self.report, expected_mode='qemu', write_result=False)
        self.assertEqual(set(result['results']), set(v.CASES))
        self.assertEqual(result['emulator_sha256'], 'a' * 64)
        self.assertFalse(result['physical_hardware'])
        self.assertFalse((self.report / 'verification.json').exists())

    def test_case_contract_matches_existing_integration(self):
        self.assertEqual(v.CASES, board.acceptance.CASES)

    def test_reported_board_never_auto_accepts_hardware(self):
        self.run.update(mode='board', emulator=None)
        self.write_run_logs()
        self.seal()
        result = v.verify_run(self.package, self.report, write_result=False)
        self.assertEqual(result['scope'], 'board-executed-headless-reported')
        self.assertFalse(result['physical_hardware'])
        self.assertEqual(result['independent_board_review'], 'pending')

    def test_missing_case_rehashed(self):
        shutil.rmtree(self.report / 'vendor-800')
        self.rejects('required evidence set incomplete')

    def test_empty_evidence(self):
        (self.report / 'SHA256SUMS').write_text('')
        self.rejects('empty SHA256SUMS', seal=False)

    def test_missing_environment_rehashed(self):
        (self.report / 'environment.log').unlink()
        self.rejects('required evidence set incomplete')

    def test_startup_case_order(self):
        path = self.report / 'startup.log'
        path.write_text(path.read_text().replace('IPC_CASE_BEGIN example-600', 'IPC_CASE_BEGIN vendor-600', 1))
        self.rejects('launcher case order/completion invalid')

    def test_startup_failure_overrules_summary(self):
        path = self.report / 'startup.log'
        path.write_text(path.read_text() + 'IPC_BOARD_FAILED forged-summary\n')
        self.rejects('launcher case order/completion invalid')

    def test_failed_exit(self):
        self.run['exit_code'] = 1
        self.rejects('run mode/completion/scope invalid')

    def test_mode_mismatch(self):
        self.run['mode'] = 'board'
        self.rejects('board mode contains an emulator')

    def test_raw_failed_report_even_with_rehashed_files(self):
        self.raw(ok=False, error='deliberate unit failure')
        self.rejects('raw Application report invalid')

    def test_raw_duplicate_keys(self):
        path = self.report / 'example-600/report.json'
        path.write_text('{"ok":false,' + path.read_text().lstrip()[1:])
        self.rejects('duplicate JSON key')

    def test_raw_wrong_capability(self):
        self.raw('denied-command', application_capabilities=241)
        self.rejects('raw Application report invalid')

    def test_raw_denied_request_was_accepted(self):
        self.raw('denied-query', ipc_requests=1)
        self.rejects('denial/offline ordinary failure missing')

    def test_four_channel_traffic_missing(self):
        self.raw(ipc_messages=1)
        self.rejects('four channel traffic incomplete')

    def test_physical_overclaim(self):
        self.raw(physical_io=True)
        self.rejects('raw Application report invalid')

    def test_production_authentication_overclaim(self):
        self.run['production_authentication'] = True
        self.rejects('run mode/completion/scope invalid')

    def test_raw_counter_bool_not_int(self):
        self.raw(completed=True)
        self.rejects('raw Application report invalid')

    def test_ppm_magic(self):
        path = self.report / 'example-600/first.ppm'
        data = path.read_bytes()
        path.write_bytes(b'P3' + data[2:])
        self.rejects('PPM magic/dimensions/size invalid')

    def test_ppm_dimensions(self):
        path = self.report / 'example-600/first.ppm'
        path.write_bytes(path.read_bytes().replace(b'1024 600', b'1024 800', 1))
        self.rejects('PPM magic/dimensions/size invalid')

    def test_ppm_truncation(self):
        (self.report / 'example-600/first.ppm').write_bytes(b'P6\n1024 600\n255\n')
        self.rejects('PPM magic/dimensions/size invalid')

    def test_pid_reuse(self):
        path = self.report / 'example-600.log'
        path.write_text(path.read_text().replace('process_pid=1000', 'process_pid=100'))
        self.rejects('process IDs must be distinct positive integers')

    def test_group_service_mismatch(self):
        path = self.report / 'process_groups.tsv'
        path.write_text(path.read_text().replace('vendor.example.v1', 'demo.counter.v1', 1))
        self.rejects('process group service differs')

    def test_provider_wrong_socket(self):
        path = self.report / 'provider-600.log'
        path.write_text(path.read_text().replace('/tmp/pocket-ipc.unit/broker.sock', '/tmp/different.sock', 1))
        self.rejects('provider socket differs')

    def test_offline_socket_outside_private_directory(self):
        path = self.report / 'offline.log'
        path.write_text(path.read_text().replace('/tmp/pocket-ipc.unit/missing.sock', '/tmp/other/missing.sock', 1))
        self.rejects('offline socket is outside')

    def test_forced_stop_overrules_zero_exit(self):
        path = self.report / 'provider-600.log'
        path.write_text(path.read_text().replace('\nexit=0', '\nRUNNER_FORCED_STOP pid=101\nexit=0'))
        self.rejects('recorded runner failure')

    def test_empty_binary_mapping(self):
        self.manifest['binaries'] = {}
        self.rejects('package identity set incomplete')

    def test_package_source_script_drift(self):
        (self.package / 'run-ipc.sh').write_text('changed unit-only script\n')
        self.rejects('shipped script/document source drift')

    def test_original_elf_interpreter(self):
        path = self.package / 'pocket-ipcd'
        data = bytearray(path.read_bytes())
        struct.pack_into('<I', data, 52, 3)
        path.write_bytes(data)
        self.manifest['binaries']['pocket-ipcd'] = v.sha(path)
        self.rejects('dynamic loader/section')

    def test_original_elf_dynamic_segment(self):
        path = self.package / 'pocket-ipcd'
        data = bytearray(path.read_bytes())
        struct.pack_into('<I', data, 52, 2)
        path.write_bytes(data)
        self.manifest['binaries']['pocket-ipcd'] = v.sha(path)
        self.rejects('dynamic loader/section')

    def test_abi_log_needed(self):
        path = self.metadata / 'pocket-ipcd-elf.log'
        path.write_text(path.read_text().replace('\nexit=0', '\n(NEEDED) libc.so.6\nexit=0'))
        self.rejects('static ARM ABI log invalid')

    def test_old_run_pass_removed_before_failure(self):
        v.write_json(self.report / 'verification.json', {'status': 'passed'})
        self.raw(ok=False)
        self.rejects('raw Application report invalid')
        self.assertFalse((self.report / 'verification.json').exists())

    def test_old_global_pass_removed_before_preflight_failure(self):
        output = self.temporary / 'global'
        output.mkdir()
        v.write_json(output / 'verification.json', {'status': 'passed'})
        with patch.object(board, 'OUT', output), patch.object(board, 'prerequisites', side_effect=RuntimeError('unit preflight failure')):
            with self.assertRaisesRegex(RuntimeError, 'unit preflight failure'):
                board.verify()
        self.assertFalse((output / 'verification.json').exists())

    def test_archive_is_bound_to_extracted_bytes(self):
        archive = self.temporary / 'fixture.tar.gz'
        with tarfile.open(archive, 'w:gz') as stream:
            stream.add(self.package, arcname=board.PACKAGE_ROOT)
        unpacked = board.unpack_archive(archive, self.temporary / 'unpacked')
        board.bind_archive(archive, unpacked)
        (unpacked / 'README.md').write_text('tampered after extraction')
        with self.assertRaisesRegex(RuntimeError, 'tested extraction differs'):
            board.bind_archive(archive, unpacked)

    def test_archive_traversal_link_duplicate_and_special_refused(self):
        for problem in ('traversal', 'symlink', 'duplicate', 'fifo'):
            with self.subTest(problem=problem):
                archive = self.temporary / (problem + '.tar.gz')
                with tarfile.open(archive, 'w:gz') as stream:
                    root = tarfile.TarInfo(board.PACKAGE_ROOT)
                    root.type = tarfile.DIRTYPE
                    stream.addfile(root)
                    entry = tarfile.TarInfo(board.PACKAGE_ROOT + '/../escape' if problem == 'traversal'
                                            else board.PACKAGE_ROOT + '/member')
                    if problem == 'symlink':
                        entry.type, entry.linkname = tarfile.SYMTYPE, '/tmp/escape'
                    elif problem == 'fifo':
                        entry.type = tarfile.FIFOTYPE
                    stream.addfile(entry, io.BytesIO(b'') if entry.isfile() else None)
                    if problem == 'duplicate':
                        stream.addfile(entry, io.BytesIO(b''))
                destination = self.temporary / ('unpack-' + problem)
                with self.assertRaisesRegex(RuntimeError, 'unsafe/duplicate'):
                    board.unpack_archive(archive, destination)
                self.assertFalse(destination.exists())


class StaticAbiTests(unittest.TestCase):
    def test_exact_static_abi_helper_contract(self):
        ipc.check_static_arm_abi(ABI, 'verifier unit only')
        for invalid in ('INTERP', 'NEEDED'):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(RuntimeError, 'static hard-float ABI'):
                ipc.check_static_arm_abi(ABI + invalid, 'verifier unit only')
        for required in ('ELF32', 'little endian', 'ARM', 'Version5 EABI', 'hard-float ABI', 'v7', 'VFP registers'):
            with self.subTest(required=required), self.assertRaisesRegex(RuntimeError, 'static hard-float ABI'):
                ipc.check_static_arm_abi(ABI.replace(required, 'absent'), 'verifier unit only')


if __name__ == '__main__':
    unittest.main()
