"""Break software evidence deliberately; these fixtures are never runtime proof."""
from __future__ import annotations
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import ipc
from runtime_boundary import read_policy


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.out = Path(self.temporary.name)
        self.replace_out = patch.object(ipc, 'OUT', self.out)
        self.replace_out.start()
        self.current = {'commit': '1' * 40, 'tree': '2' * 40, 'development': False,
                        'source_files': {'hosts/linux/ipc/wire.c': ipc.sha(ROOT / 'hosts/linux/ipc/wire.c')}}
        self.directory = self.out / 'native'
        self.runs = self.directory / 'tests-1'
        self.runs.mkdir(parents=True)
        policy = read_policy()
        self.build = {**self.current, 'mode': 'native', 'sanitizer': False, 'gcc': '11.4.0',
                      'binaries': {}, 'ownership': {}, 'logs': {}}
        for name, (inputs, consumer) in ipc.TARGETS.items():
            # Opaque dummy bytes are only corruption-test inputs. No fixture is
            # executed and no test accepts them as a real IPC implementation.
            binary = self.directory / name
            binary.write_bytes(b'negative-verifier-fixture:' + name.encode())
            ownership = {**{k: self.current[k] for k in ('development',)},
                         'source_commit': self.current['commit'], 'source_tree': self.current['tree'],
                         'binary_sha256': ipc.sha(binary), 'consumer': consumer,
                         'runtime_input_policy_passed': True,
                         'ownership_manifest_sha256': ipc.sha(ROOT / 'contracts/runtime-ownership.json'),
                         'inputs': {n: policy['link_inputs'][n] for n in inputs},
                         'sha256': {n: ipc.sha(ROOT / n) for n in inputs}}
            ipc.write(binary.with_suffix('.ownership.json'), ownership)
            self.build['binaries'][name] = ipc.sha(binary)
            self.build['ownership'][name] = ipc.sha(binary.with_suffix('.ownership.json'))
            log = self.directory / (name + '-build.log')
            log.write_text('$ gcc negative-unit-fixture\nexit=0\n')
            self.build['logs'][log.name] = ipc.sha(log)
        (self.directory / 'elf.log').write_text('$ readelf negative-unit-fixture\nexit=0\n')
        self.build['abi_sha256'] = ipc.sha(self.directory / 'elf.log')
        logs = {}
        for name, marker in ipc.MARKERS.items():
            path = self.runs / name
            path.write_text('$ negative-verifier-fixture\n' + marker + '\nexit=0\n')
            logs[name] = ipc.sha(path)
        self.record = {**self.current, 'mode': 'native', 'sanitizer': False,
                       'physical_hardware': False, 'scope': 'real-socket-software-functional',
                       'run_dir': 'native/tests-1', 'binaries': dict(self.build['binaries']), 'logs': logs}
        self.save()

    def tearDown(self):
        self.replace_out.stop()
        self.temporary.cleanup()

    def save(self):
        ipc.write(self.directory / 'build.json', self.build)
        self.record['build_sha256'] = ipc.sha(self.directory / 'build.json')
        ipc.write(self.directory / 'test.json', self.record)

    def rejects(self, reason):
        self.save()
        with self.assertRaisesRegex((RuntimeError, OSError), reason):
            ipc.verify_record('native', self.current)

    def test_original_empty_evidence_reproduction(self):
        self.record.update(binaries={}, logs={}, run_dir='missing-tests')
        self.build = {}
        self.rejects('incomplete IPC evidence set: test binaries')

    def test_missing_test_binary_entry(self):
        del self.record['binaries']['test-service']
        self.rejects('incomplete IPC evidence set: test binaries')

    def test_empty_or_missing_sdk_log_set(self):
        self.record['logs'] = {}
        self.rejects('incomplete IPC evidence set: test logs')

    def test_cross_directory_evidence_is_rejected(self):
        self.record['run_dir'] = 'native/../elsewhere'
        self.rejects('invalid IPC evidence directory')

    def test_stale_build_even_with_matching_record_digest(self):
        self.build['commit'] = '3' * 40
        self.rejects('stale or wrong IPC build source')

    def test_wrong_compiler_is_rejected(self):
        self.build['gcc'] = '13.3.0'
        self.rejects('IPC compiler baseline not demonstrated')

    def test_changed_binary_is_rejected(self):
        (self.directory / 'pocket-ipcd').write_bytes(b'changed')
        self.rejects('IPC binary/ownership evidence drift')

    def test_missing_binary_is_rejected(self):
        (self.directory / 'test-core').unlink()
        self.rejects('No such file')

    def test_rehashed_log_without_completion_is_rejected(self):
        path = self.runs / 'test-service.log'
        path.write_text('$ test-service\nexit=0\n')
        self.record['logs'][path.name] = ipc.sha(path)
        self.rejects('IPC completion marker missing')

    def test_rehashed_failed_exit_is_rejected(self):
        path = self.runs / 'test-core.log'
        path.write_text('$ test-core\nIPC_CORE_OK\nexit=1\n')
        self.record['logs'][path.name] = ipc.sha(path)
        self.rejects('IPC successful process exit not recorded')

    def test_test_consumer_cannot_replace_production_broker(self):
        path = self.directory / 'pocket-ipcd.ownership.json'
        ownership = json.loads(path.read_text())
        ownership['consumer'] = 'runtime-test'
        ipc.write(path, ownership)
        self.build['ownership']['pocket-ipcd'] = ipc.sha(path)
        self.rejects('incorrect IPC build consumer')

    def test_rehashed_empty_dependency_hashes_are_rejected(self):
        path = self.directory / 'pocket-ipcd.ownership.json'
        ownership = json.loads(path.read_text())
        ownership['sha256'] = {}
        ipc.write(path, ownership)
        self.build['ownership']['pocket-ipcd'] = ipc.sha(path)
        self.rejects('IPC dependency hashes incomplete')

    def test_old_pass_is_removed_before_failure(self):
        old = self.out / 'verification.json'
        ipc.write(old, {'status': 'passed'})
        self.record['binaries'] = {}
        self.save()
        with patch.object(ipc, 'source', return_value=self.current):
            with self.assertRaisesRegex(RuntimeError, 'incomplete IPC evidence set'):
                ipc.verify()
        self.assertFalse(old.exists())


if __name__ == '__main__':
    unittest.main()
