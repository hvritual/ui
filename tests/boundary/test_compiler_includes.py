"""An admitted C data include must remain distinct from an unknown source."""
import unittest
import test_runtime_boundary as fixtures


class CompilerIncludeTests(unittest.TestCase):
    setUp = fixtures.BoundaryTests.setUp
    write = fixtures.BoundaryTests.write
    save = fixtures.BoundaryTests.save
    validate = fixtures.BoundaryTests.validate

    def test_explicit_runtime_data_include_is_hashed_and_admitted(self):
        name = 'hosts/linux/ui/data.c'
        self.write(name, 'static const int value = 0;\n')
        self.write(self.runtime, '#include "data.c"\nint main(void) { return value; }\n')
        with self.assertRaisesRegex(fixtures.boundary.BoundaryError, 'unclassified'):
            self.validate(consumer='runtime')
        self.policy['link_inputs'][name] = {'role':'runtime','reference_only':False,'origin':'repository'}
        self.save()
        report = self.validate(consumer='runtime')
        self.assertIn(name, report['compiler_dependencies'][self.runtime])
        self.assertIn(name, report['sha256'])
        self.assertFalse(report['application_separation'])
