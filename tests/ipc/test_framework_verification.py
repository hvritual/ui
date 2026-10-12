"""Corrupt verifier fixtures; no fixture is executed or used as runtime proof."""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import ipc
import ipc_acceptance as acceptance
import pui
from runtime_boundary import read_policy


class FrameworkEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        temporary = Path(self.temporary.name)
        self.out = temporary / 'framework'
        self.ipc_out = temporary / 'ipc'
        self.patchers = [patch.object(acceptance, 'OUT', self.out), patch.object(ipc, 'OUT', self.ipc_out)]
        for patcher in self.patchers:
            patcher.start()
        self.current = {'commit': '1' * 40, 'tree': '2' * 40, 'development': False,
                        'source_files': {'scripts/ipc_acceptance.py': ipc.sha(ROOT / 'scripts/ipc_acceptance.py')}}
        self.directory = self.out / 'native/ipc-processes-1'
        self.directory.mkdir(parents=True)
        self.runtime = self.out / 'native/ui-framework'
        self.runtime.write_bytes(b'opaque verifier fixture; never an executed UI')
        policy = read_policy()

        def ownership(binary, inputs, consumer):
            value = {'binary_sha256': ipc.sha(binary), 'source_commit': self.current['commit'],
                     'source_tree': self.current['tree'], 'development': False,
                     'consumer': consumer, 'runtime_input_policy_passed': True,
                     'ownership_manifest_sha256': ipc.sha(ROOT / 'contracts/runtime-ownership.json'),
                     'inputs': {name: policy['link_inputs'][name] for name in inputs},
                     'sha256': {name: ipc.sha(ROOT / name) for name in inputs}}
            ipc.write(binary.with_suffix('.ownership.json'), value)

        ownership(self.runtime, ['hosts/linux/application/application.c', 'hosts/linux/application/program.c',
                  'hosts/linux/ipc/wire.c', 'hosts/linux/ipc/client.c', 'hosts/linux/package/loaded.c'], 'runtime')
        (self.directory / 'executables').mkdir()
        for name in ('pocket-ipcd', 'service-fixture'):
            binary = self.ipc_out / 'native' / name
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.write_bytes(b'opaque verifier fixture; never executed:' + name.encode())
            ownership(binary, *ipc.TARGETS[name])
            shutil.copy2(binary, self.directory / 'executables' / name)
            shutil.copy2(binary.with_suffix('.ownership.json'), self.directory / 'executables' / (name + '.ownership.json'))
            (self.directory / 'executables' / (name + '-build.log')).write_text('$ verifier-unit-compiler-fixture\nexit=0\n')

        for package_name in {package_name for _, package_name, _ in acceptance.CASES.values()}:
            assets = self.directory / (package_name + '-source')
            assets.mkdir()
            for name in pui.REQUIRED:
                (assets / name).write_bytes(b'verifier-only opaque asset')
            manifest = pui.load_manifest(ROOT / 'contracts/application-package.example.json')
            manifest['app_id'] = 'com.pocket.' + package_name
            manifest['capabilities'] = acceptance.case_caps(package_name)
            manifest['budgets']['heap_bytes'] = 2 * 1024 * 1024
            (self.directory / (package_name + '.pui')).write_bytes(pui.build(manifest, assets))
            ipc.write(self.directory / (package_name + '.manifest.json'), manifest)

        images = {}
        for height in (600, 800):
            for variant in ('first', 'last'):
                path = temporary / (str(height) + '-' + variant + '.ppm')
                path.write_bytes(('P6\n1024 ' + str(height) + '\n255\n').encode() +
                                 bytes([0 if variant == 'first' else 1]) * (1024 * height * 3))
                images[height, variant] = path
        results = {}
        for name, (height, package_name, selection) in acceptance.CASES.items():
            destination = self.directory / name
            destination.mkdir()
            package_path = self.directory / (package_name + '.pui')
            caps = acceptance.case_caps(package_name)
            profile = 'imx6ul-1024x' + str(height)
            report = {'schema': 1, 'commit': self.current['commit'], 'profile': profile,
                      'ok': True, 'error': None, 'completed': 1, 'application_selection': selection,
                      'package_admitted': True, 'package_sha256': ipc.sha(package_path),
                      'package_bytes': package_path.stat().st_size, 'application_heap_limit': 2 * 1024 * 1024,
                      'application_capabilities': sum(pui.CAPABILITIES[cap] for cap in caps),
                      'ipc_development': True, 'ipc_command_capable': 'ipc.command' in caps,
                      'physical_io': False, 'visual_validated': False, 'business_commands': False,
                      'package_authenticated': False, 'core_live_bytes_after_close': 0, 'ipc_pending': 0,
                      'ticks': 180, 'presents': 90, 'item_count': int(selection == 3),
                      'ipc_requests': 9 if selection == 3 else 0, 'ipc_messages': 14 if selection == 3 else 0,
                      'ipc_failures': 0 if selection == 3 else 1, 'ipc_ready': name != 'offline',
                      'ipc_status': 3 if name == 'offline' else 0}
            ipc.write(destination / 'report.json', report)
            for variant in ('first', 'last'):
                os.link(images[height, variant], destination / (variant + '.ppm'))
            (destination / 'timeline.csv').write_text('sample_ns,input_event_ns,verifier-fixture-only\n1,0,0\n')
            (self.directory / (name + '.log')).write_text(
                '$ ' + str(self.runtime) + ' --headless --profile ' + profile +
                ' --package ' + str(package_path) + ' --allow-unsigned-package --ipc-development-socket /tmp/unit.sock --seconds 3 --output ' + str(destination) + '\n' +
                'FRAMEWORK_FIRST_PRESENT profile=' + profile + ' mode=headless commit=' + self.current['commit'] + '\n' +
                'FRAMEWORK_EXIT ok=1 error=none output=' + str(destination) + '\nexit=0\n')
            results[name] = {'runtime_sha256': ipc.sha(self.runtime), 'package_sha256': ipc.sha(package_path),
                             'selection': selection, 'completed': 1, 'ipc_requests': report['ipc_requests'],
                             'ipc_messages': report['ipc_messages'], 'first': ipc.sha(destination / 'first.ppm'),
                             'last': ipc.sha(destination / 'last.ppm')}
        groups = []
        for height, start in ((600, 100), (800, 200)):
            groups.append({'profile': 'imx6ul-1024x' + str(height), 'broker_pid': start,
                           'provider_pids': [start + 1, start + 2], 'dynamic_registration_after_first_ui': True})
            for stem, pid, service in (('broker', start, None), ('provider', start + 1, 'demo.counter.v1'),
                                      ('vendor-provider', start + 2, 'vendor.example.v1')):
                binary = self.ipc_out / 'native' / ('service-fixture' if service else 'pocket-ipcd')
                text = '$ ' + str(binary) + ' --socket /tmp/unit-' + str(height) + '.sock'
                text += ' --service ' + service if service else ' --client-uid 1000 --service-uid 1000'
                if stem == 'vendor-provider':
                    text += ' --query read --command increase --event updated --state values'
                text += '\nprocess_pid=' + str(pid) + '\n'
                if service:
                    text += 'SERVICE_READY service=' + service + ' epoch=1 broker=1 pid=' + str(pid) + '\n'
                    text += 'SERVICE_STOPPED synthetic-counter-only; no-device-admission; process-local-dedup\n'
                (self.directory / (stem + '-' + str(height) + '.log')).write_text(text + '\nexit=0\n')
        self.record = {**self.current, 'mode': 'native', 'runtime_sha256': ipc.sha(self.runtime),
                       'broker_sha256': ipc.sha(self.ipc_out / 'native/pocket-ipcd'),
                       'provider_sha256': ipc.sha(self.ipc_out / 'native/service-fixture'),
                       'directory': 'native/ipc-processes-1', 'same_binary_dynamic_services': True,
                       'real_application_vm': True, 'real_independent_processes': True,
                       'physical_hardware': False, 'production_authentication': False,
                       'process_groups': groups, 'results': results}
        self.save()

    def tearDown(self):
        for patcher in reversed(self.patchers):
            patcher.stop()
        self.temporary.cleanup()

    def save(self, rehash=True):
        if rehash:
            self.record['evidence'] = acceptance.files(self.directory)
        ipc.write(self.out / 'native/ipc-proof.json', self.record)

    def rejects(self, pattern, rehash=True):
        self.save(rehash)
        with self.assertRaisesRegex((RuntimeError, OSError, ValueError), pattern):
            acceptance.verify_record('native', self.current)

    def report(self, name='example-600', **changes):
        path = self.directory / name / 'report.json'
        data = json.loads(path.read_text())
        data.update(changes)
        ipc.write(path, data)

    def test_complete_verifier_fixture_structure(self):
        self.assertEqual(set(acceptance.verify_record('native', self.current)['results']), set(acceptance.CASES))

    def test_original_empty_evidence_reproduction(self):
        shutil.rmtree(self.directory)
        self.directory.mkdir()
        self.record['evidence'] = {}
        self.rejects('required raw evidence incomplete', rehash=False)

    def test_nine_arbitrary_case_names_are_rejected(self):
        self.record['results']['arbitrary-ninth-case'] = self.record['results'].pop('offline')
        self.rejects('profile/capability matrix incomplete')

    def test_wrong_mode(self):
        self.record['mode'] = 'arm'
        self.rejects('stale or overstated')

    def test_escaped_directory(self):
        self.record['directory'] = 'native/../ipc-processes-1'
        self.rejects('invalid IPC Framework evidence directory')

    def test_package_mapping_is_verified_from_container(self):
        shutil.copyfile(self.directory / 'ipc-vendor.pui', self.directory / 'ipc-example.pui')
        self.rejects('case/package capability mapping differs')

    def test_rehashed_wrong_profile_report(self):
        self.report(profile='imx6ul-1024x800')
        self.rejects('raw Application report invalid')

    def test_rehashed_failed_report(self):
        self.report(ok=False, error='failed')
        self.rejects('raw Application report invalid')

    def test_rehashed_device_overclaim(self):
        self.report(business_commands=True)
        self.rejects('raw Application report invalid')

    def test_boolean_is_not_a_success_counter(self):
        self.report(completed=True)
        self.rejects('raw Application report invalid')

    def test_rehashed_four_channel_claim_without_traffic(self):
        self.report(ipc_requests=1)
        self.record['results']['example-600']['ipc_requests'] = 1
        self.rejects('four channel traffic incomplete')

    def test_denied_channel_must_not_be_accepted(self):
        self.report('denied-command', ipc_requests=1)
        self.record['results']['denied-command']['ipc_requests'] = 1
        self.rejects('denial/offline ordinary failure missing')

    def test_summary_must_match_raw_report(self):
        self.record['results']['example-600']['selection'] = 10
        self.rejects('summarized result differs')

    def test_rehashed_invalid_ppm_magic(self):
        path = self.directory / 'example-600/first.ppm'
        data = path.read_bytes()
        path.write_bytes(b'P3' + data[2:])
        self.record['results']['example-600']['first'] = ipc.sha(path)
        self.rejects('rendered PPM magic, dimensions or size invalid')

    def test_rehashed_wrong_ppm_dimensions(self):
        path = self.directory / 'example-600/first.ppm'
        path.write_bytes(path.read_bytes().replace(b'1024 600', b'1024 800', 1))
        self.rejects('rendered PPM magic, dimensions or size invalid')

    def test_rehashed_truncated_ppm(self):
        (self.directory / 'example-600/first.ppm').write_bytes(b'P6\n1024 600\n255\n')
        self.rejects('rendered PPM magic, dimensions or size invalid')

    def test_actual_pixel_hash_must_match_summary(self):
        self.record['results']['example-600']['last'] = 'a' * 64
        self.rejects('summarized result differs')

    def test_rehashed_identical_first_last_frame(self):
        first = self.directory / 'example-600/first.ppm'
        first.write_bytes((self.directory / 'example-600/last.ppm').read_bytes())
        self.rejects('did not change rendered pixels')

    def test_required_binary_cannot_be_removed_and_rehashed(self):
        (self.directory / 'executables/pocket-ipcd').unlink()
        self.rejects('required raw evidence incomplete')

    def test_rehashed_binary_drift(self):
        (self.directory / 'executables/pocket-ipcd').write_bytes(b'different-verifier-fixture')
        self.rejects('saved executable identity drift')

    def test_rehashed_empty_ownership_dependencies(self):
        path = self.directory / 'executables/pocket-ipcd.ownership.json'
        data = json.loads(path.read_text())
        data['sha256'] = {}
        ipc.write(path, data)
        self.rejects('dependency hashes incomplete')

    def test_rehashed_failed_background_exit(self):
        path = self.directory / 'broker-600.log'
        path.write_text(path.read_text().replace('exit=0', 'exit=1'))
        self.rejects('successful process exit not recorded')

    def test_rehashed_provider_log_without_readiness(self):
        path = self.directory / 'provider-600.log'
        path.write_text('\n'.join(line for line in path.read_text().split('\n') if not line.startswith('SERVICE_READY')))
        self.rejects('readiness/shutdown evidence incomplete')

    def test_rehashed_ui_log_without_completion(self):
        path = self.directory / 'example-600.log'
        path.write_text('\n'.join(line for line in path.read_text().split('\n') if not line.startswith('FRAMEWORK_EXIT')))
        self.rejects('completion marker missing')

    def test_duplicate_or_nonpositive_process_ids(self):
        self.record['process_groups'][0]['provider_pids'][0] = self.record['process_groups'][0]['broker_pid']
        self.rejects('distinct positive integers')

    def test_false_pid_is_not_an_integer(self):
        self.record['process_groups'][0]['broker_pid'] = False
        self.rejects('distinct positive integers')

    def test_process_group_profile_matrix(self):
        self.record['process_groups'][1]['profile'] = 'imx6ul-1024x600'
        self.rejects('process group matrix incomplete')

    def test_old_pass_removed_before_verifier_failure(self):
        old = self.out / 'ipc-verification.json'
        ipc.write(old, {'status': 'passed'})
        self.record['results'] = {}
        self.save()
        with patch.object(ipc, 'source', return_value=self.current), self.assertRaisesRegex(RuntimeError, 'matrix incomplete'):
            acceptance.verify()
        self.assertFalse(old.exists())

    def test_old_proof_removed_before_new_execution_can_fail(self):
        old = self.out / 'native/ipc-proof.json'
        ipc.write(self.out / 'ipc-verification.json', {'status': 'passed'})
        with patch.object(ipc, 'source', side_effect=RuntimeError('stop-before-execution')), self.assertRaisesRegex(RuntimeError, 'stop-before-execution'):
            acceptance.test('native')
        self.assertFalse(old.exists())
        self.assertFalse((self.out / 'ipc-verification.json').exists())


if __name__ == '__main__':
    unittest.main()
