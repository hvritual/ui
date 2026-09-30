"""Real compiler-dependency checks using tiny test-owned source fixtures only."""
from __future__ import annotations
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('boundary', ROOT/'scripts/runtime_boundary.py')
boundary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boundary)


class BoundaryTests(unittest.TestCase):
    def setUp(self):
        if shutil.which('gcc') is None:
            raise RuntimeError('gcc required; compiler-dependent gates must not skip')
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runtime = 'hosts/linux/ui/unit.c'
        self.app = 'apps/example/app.c'
        self.test = 'tests/example/test.c'
        self.adapter = 'hosts/linux/framework.c'
        self.header = 'apps/example/value.h'
        for name in (self.runtime, self.app, self.test, self.adapter):
            self.write(name, 'int main(void) { return 0; }\n')
        self.write(self.header, '#define APP_VALUE 0\n')
        self.write('hosts/linux/engine/scene_guest.js', '// test-owned adapter fixture\n')
        self.write('Makefile', '\tpython3 scripts/framework_guard.py test --mode native\n')
        self.policy = {'schema_version': 1, 'link_inputs': {
            self.runtime: {'role': 'runtime', 'reference_only': False, 'origin': 'repository'},
            self.app: {'role': 'application', 'reference_only': True, 'origin': 'repository'},
            self.test: {'role': 'test-only', 'reference_only': False, 'origin': 'repository'},
            self.adapter: {'role': 'adapter', 'reference_only': True, 'origin': 'repository'},
        }, 'reference_headers': ['hosts/linux/framework.h'],
           'private_payloads': {'hosts/linux/engine/scene_guest.js': 'adapter-not-developer-asset'}}
        self.save()
        self.command = ['gcc', '-std=c11', '-I.', self.runtime, '-o', 'out/unit']

    def write(self, name, text):
        path = self.root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def save(self):
        self.write('contracts/runtime-ownership.json', json.dumps(self.policy))

    def validate(self, command=None, consumer='reference'):
        return boundary.validate_command(command or self.command, self.root, consumer)

    def test_platform_fixture_is_allowed(self):
        result = self.validate(consumer='runtime')
        self.assertTrue(result['runtime_input_policy_passed'])
        self.assertFalse(result['application_separation'])
        self.assertFalse(result['physical_hardware'])
        self.assertEqual(result['compiler_dependencies'][self.runtime], [self.runtime])
        self.assertFalse((self.root/'out/unit').exists())

    def test_reference_monolith_is_not_separation(self):
        result = self.validate(['gcc', self.app, '-o', 'out/unit'])
        self.assertFalse(result['application_separation'])

    def test_application_in_runtime_rejected(self):
        with self.assertRaisesRegex(boundary.BoundaryError, 'fixed runtime'):
            self.validate(['gcc', self.app, '-o', 'out/unit'], 'runtime')

    def test_reference_adapter_in_runtime_rejected(self):
        with self.assertRaisesRegex(boundary.BoundaryError, 'fixed runtime'):
            self.validate(['gcc', self.adapter, '-o', 'out/unit'], 'runtime')

    def test_runtime_test_still_rejects_application(self):
        with self.assertRaises(boundary.BoundaryError):
            self.validate(['gcc', self.app, '-o', 'out/unit'], 'runtime-test')

    def test_test_source_in_deployment_rejected(self):
        with self.assertRaisesRegex(boundary.BoundaryError, 'deployment'):
            self.validate(['gcc', self.test, '-o', 'out/unit'])
        self.validate(['gcc', self.test, '-o', 'out/unit'], 'reference-test')

    def test_new_business_translation_unit_rejected(self):
        self.write('apps/other/app.c', 'int other(void) { return 0; }\n')
        with self.assertRaisesRegex(boundary.BoundaryError, 'unclassified'):
            self.validate(['gcc', 'apps/other/app.c', '-o', 'out/unit'])

    def test_unknown_object_rejected(self):
        self.write('out/unreviewed.o', 'not an object')
        with self.assertRaisesRegex(boundary.BoundaryError, 'unclassified'):
            self.validate(self.command + ['out/unreviewed.o'])

    def test_response_and_opaque_flags_rejected(self):
        for option in ('@inputs', '-Wl,@inputs', '-includehidden.h', '-fplugin=x', '-Xlinker', '-Tlayout.ld', '-lcoffee', '-isystem/tmp', '-x'):
            with self.subTest(option=option), self.assertRaises(boundary.BoundaryError):
                self.validate(self.command + [option])

    def test_deployment_linker_wrap_rejected(self):
        with self.assertRaises(boundary.BoundaryError):
            self.validate(self.command + ['-Wl,--wrap=host_open'])
        self.validate(self.command + ['-Wl,--wrap=host_open'], 'reference-test')

    def test_transitive_application_header_rejected_by_real_preprocessor(self):
        self.write(self.runtime, '#include "shared.h"\nint main(void) { return APP_VALUE; }\n')
        self.write('hosts/linux/ui/shared.h', '#include "../../../apps/example/value.h"\n')
        with self.assertRaisesRegex(boundary.BoundaryError, 'imports application'):
            self.validate()
        self.assertFalse((self.root/'out/unit').exists())

    def test_included_application_c_rejected(self):
        self.write(self.runtime, '#include "../../../apps/example/app.c"\n')
        with self.assertRaises(boundary.BoundaryError):
            self.validate()

    def test_test_header_in_platform_rejected(self):
        self.write('tests/example/test.h', '#define VALUE 0\n')
        self.write(self.runtime, '#include "../../../tests/example/test.h"\nint main(void) { return VALUE; }\n')
        with self.assertRaises(boundary.BoundaryError):
            self.validate(consumer='reference-test')

    def test_private_reference_header_rejected(self):
        self.write('hosts/linux/framework.h', '#define VALUE 0\n')
        self.write(self.runtime, '#include "../framework.h"\nint main(void) { return VALUE; }\n')
        with self.assertRaises(boundary.BoundaryError):
            self.validate()

    def test_dependency_failure_fails_closed(self):
        self.write(self.runtime, '#include "missing.h"\n')
        with self.assertRaisesRegex(boundary.BoundaryError, 'preprocessing failed'):
            self.validate()

    def test_symlink_escape_rejected(self):
        external = self.root.parent/(self.root.name+'-outside.c')
        external.write_text('int external;\n')
        self.addCleanup(external.unlink)
        (self.root/self.runtime).unlink()
        (self.root/self.runtime).symlink_to(external)
        with self.assertRaisesRegex(boundary.BoundaryError, 'escapes'):
            self.validate()

    def test_escaped_include_directory_rejected(self):
        with self.assertRaisesRegex(boundary.BoundaryError, 'include directory'):
            self.validate(self.command + ['-I'+str(self.root.parent)])

    def test_duplicate_inputs_and_output_rejected(self):
        for extra in ([self.runtime], ['-o', 'out/other']):
            with self.subTest(extra=extra), self.assertRaises(boundary.BoundaryError):
                self.validate(self.command + extra)

    def test_invalid_output_and_consumer_rejected(self):
        for output in (self.runtime, '../escaped', 'source-file'):
            with self.subTest(output=output), self.assertRaises(boundary.BoundaryError):
                self.validate(['gcc', self.runtime, '-o', output])
        with self.assertRaises(boundary.BoundaryError):
            self.validate(consumer='all-allowed')

    def test_application_role_cannot_be_laundered(self):
        self.policy['link_inputs'][self.app]['role'] = 'runtime'
        self.save()
        with self.assertRaisesRegex(boundary.BoundaryError, 'reclassified'):
            self.validate()

    def test_test_role_cannot_be_laundered(self):
        self.policy['link_inputs'][self.test]['role'] = 'runtime'
        self.save()
        with self.assertRaisesRegex(boundary.BoundaryError, 'reclassified'):
            self.validate()

    def test_generated_input_requires_provenance(self):
        self.policy['link_inputs']['out/unknown.a'] = {'role':'runtime','reference_only':False,'origin':'repository'}
        self.save()
        with self.assertRaisesRegex(boundary.BoundaryError, 'provenance'):
            self.validate()

    def test_duplicate_json_keys_and_bool_version_rejected(self):
        self.write('contracts/runtime-ownership.json', '{"schema_version":1,"schema_version":1}')
        with self.assertRaisesRegex(boundary.BoundaryError, 'duplicate'):
            self.validate()
        self.policy['schema_version'] = True
        self.save()
        with self.assertRaisesRegex(boundary.BoundaryError, 'schema'):
            self.validate()

    def test_private_adapter_cannot_be_declared_public_asset(self):
        self.policy['private_payloads']['hosts/linux/engine/scene_guest.js'] = 'public'
        self.save()
        with self.assertRaises(boundary.BoundaryError):
            self.validate()

    def test_static_inventory_has_no_separation_claim(self):
        result = boundary.check_checkout(self.root)
        self.assertFalse(result['application_separation'])
        self.write('Makefile', '\tpython3 scripts/framework.py package\n')
        with self.assertRaisesRegex(boundary.BoundaryError, 'guarded builder'):
            boundary.check_checkout(self.root)

    def test_hash_changes_with_source(self):
        first = self.validate()['sha256'][self.runtime]
        self.write(self.runtime, 'int main(void) { return 1; }\n')
        self.assertNotEqual(first, self.validate()['sha256'][self.runtime])

    def test_validation_does_not_replace_or_compile_runtime(self):
        record = self.validate()
        (self.root/'out').mkdir()
        subprocess.run(self.command, cwd=self.root, check=True, capture_output=True)
        subprocess.run([str(self.root/'out/unit')], check=True)
        self.assertEqual(record['scope'], 'link-input-ownership-only')


if __name__ == '__main__':
    unittest.main()
