"""System-header warning policy must not hide application dependencies."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('runtime_boundary',ROOT/'scripts/runtime_boundary.py')
boundary=importlib.util.module_from_spec(spec);spec.loader.exec_module(boundary)


class SystemIncludeBoundary(unittest.TestCase):
    def test_dependency_classification_and_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for directory in ('out/dependency','apps','contracts'):(root/directory).mkdir(parents=True)
            (root/'native.c').write_text('#include <external.h>\nint main(void){return VALUE;}\n')
            (root/'out/dependency/external.h').write_text('#define VALUE 0\n')
            policy={'schema_version':1,'link_inputs':{'native.c':{'role':'runtime','reference_only':False,'origin':'repository'}},
                    'private_payloads':{'adapter.js':'adapter-not-developer-asset'}}
            (root/'contracts/runtime-ownership.json').write_text(json.dumps(policy))
            command=['gcc','-std=c11','-isystemout/dependency','native.c','-o','out/program']
            report=boundary.validate_command(command,root,'runtime')
            self.assertIn('out/dependency/external.h',report['compiler_dependencies']['native.c'])
            (root/'apps/secret.h').write_text('#define VALUE 0\n')
            (root/'out/dependency/external.h').write_text('#include "../../apps/secret.h"\n')
            with self.assertRaises(boundary.BoundaryError):boundary.validate_command(command,root,'runtime')
            for flag in ('-isystem/usr/include','-isystemapps'):
                with self.assertRaises(boundary.BoundaryError):
                    boundary.validate_command(['gcc',flag,'native.c','-o','out/program'],root,'runtime')
            (root/'out/link').symlink_to('/usr/include',target_is_directory=True)
            with self.assertRaises(boundary.BoundaryError):
                boundary.validate_command(['gcc','-isystemout/link','native.c','-o','out/program'],root,'runtime')


if __name__=='__main__':unittest.main()
