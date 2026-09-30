"""Exercise the real wrapper seam without claiming a fake framework is the UI."""
from __future__ import annotations
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import types
import unittest
from unittest import mock

import test_runtime_boundary as fixtures
ROOT, boundary = fixtures.ROOT, fixtures.boundary


class FrameworkGuardTests(unittest.TestCase):
    setUp = fixtures.BoundaryTests.setUp
    write = fixtures.BoundaryTests.write
    save = fixtures.BoundaryTests.save
    # Reuse the source-fixture setup, but only add wrapper-specific cases here.
    def test_wrapper_invokes_unchanged_compiler_and_records_ownership(self):
        fake = types.ModuleType('framework')
        fake.ROOT = self.root
        fake.git = lambda *args: '1'*40 if args[-1] == 'HEAD' else '2'*40
        fake.state = lambda: {'development': True}
        calls = []

        def run(args, log, expected=0, timeout=900):
            calls.append(list(args))
            result = subprocess.run(args, cwd=self.root, capture_output=True, timeout=timeout)
            if result.returncode != expected:
                raise RuntimeError(result.stderr.decode())
            return result.stdout.decode()
        fake.run = run

        def compile_binary(mode, test=False, sanitize=False, static=False, loop=False):
            (self.root/'out').mkdir(exist_ok=True)
            fake.run(self.command, self.root/'out/build.log')
            return self.root/'out/unit'
        fake.compile_binary = compile_binary
        spec = importlib.util.spec_from_file_location('framework_guard_under_test', ROOT/'scripts/framework_guard.py')
        module = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'framework':fake,'runtime_boundary':boundary}):
            spec.loader.exec_module(module)
            module.install()
            binary = fake.compile_binary('native')
        self.assertIs(fake.run, run)
        self.assertEqual(calls, [self.command])
        record = json.loads(binary.with_suffix('.ownership.json').read_text())
        self.assertEqual(record['source_commit'], '1'*40)
        self.assertEqual(record['binary_sha256'], hashlib.sha256(binary.read_bytes()).hexdigest())
        self.assertFalse(record['application_separation'])
        self.assertTrue(record['development'])
        self.assertEqual(record['scope'], 'link-input-ownership-only')

    def test_wrapper_restores_runner_and_never_compiles_rejected_source(self):
        fake = types.ModuleType('framework')
        fake.ROOT = self.root
        self.write('apps/unknown.c','int main(void){return 0;}\n')
        fake.run = mock.Mock(side_effect=AssertionError('compiler must not execute'))
        original_run = fake.run
        def compile_binary(*args):
            fake.run(['gcc','apps/unknown.c','-o','out/unit'],self.root/'out/build.log')
        fake.compile_binary = compile_binary
        spec = importlib.util.spec_from_file_location('framework_guard_rejection', ROOT/'scripts/framework_guard.py')
        module = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {'framework':fake,'runtime_boundary':boundary}):
            spec.loader.exec_module(module)
            module.install()
            with self.assertRaises(boundary.BoundaryError):
                fake.compile_binary('native')
        self.assertIs(fake.run, original_run)
        original_run.assert_not_called()
        self.assertFalse((self.root/'out/unit').exists())
