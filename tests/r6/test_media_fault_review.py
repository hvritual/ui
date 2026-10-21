"""Synthetic data-only tests. No fixture is a physical board acceptance."""
import json
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import r6_hil as gate
import r6_media_fault_review as review

A = 'a' * 64
B = 'b' * 64
STAMP = '2026-10-11T00:00:00Z'


class MediaFaultReview(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.bundle = self.root / 'bundle'
        self.bundle.mkdir()
        gate.fixture(self.bundle)
        self.run = self.bundle / 'returned/imx6ul-1024x600/coffee'
        self.session = self.root / 'session'
        self.session.mkdir()
        (self.session / 'store').mkdir()
        (self.bundle / 'updates').mkdir()
        for path, data in (
            ('mediactl', b'SYNTHETIC NONEXECUTABLE mediactl'),
            ('updates/demo.public', b'SYNTHETIC NOT PUBLIC KEY'),
            ('updates/demo-a.zip', b'SYNTHETIC NOT SIGNED DEMO A'),
            ('updates/demo-b.zip', b'SYNTHETIC NOT SIGNED DEMO B'),
        ):
            p = self.bundle / path
            p.write_bytes(data)
            with (self.bundle / 'SHA256SUMS').open('a') as stream:
                stream.write(f'{gate.sha(p)}  ./{path}\n')
        provenance = dict(schema='R6MF1', profile='imx6ul-1024x600')
        for field, path in review.PROVENANCE.items():
            provenance[field] = gate.sha(self.bundle / path)
        (self.session / 'provenance.tsv').write_text(''.join(f'{k}\t{v}\n' for k, v in provenance.items()))
        self.events = list(review.MEDIA_SEQUENCE + review.FAULT_SEQUENCE)
        self.action_generations = {
            'A_REQUESTED': A, 'B_BUSY_REQUESTED': B, 'ROLLBACK_BUSY_REQUESTED': A
        }
        self.save_actions()
        for label, gen, action in (('install-a', A, 'install'), ('install-b', B, 'install'),
                                   ('rollback', A, 'rollback')):
            output = (f'MEDIA_INSTALLED version={"demo-a" if label == "install-a" else "demo-b"} '
                      f'requested_generation={gen} ui_application=deferred-until-safe-point\n'
                      if action == 'install' else f'MEDIA_ROLLBACK requested_generation={gen}\n')
            (self.session / f'{label}.out').write_text(output)
            gate.write_json(self.session / f'{label}.status.json', {
                'requested': gen, 'previous': B if label == 'rollback' else A if label == 'install-b' else '-',
                'note': 'requested is not proof of on-screen application; inspect UI log'
            })
        (self.session / 'store/current').write_text(A + '\n' + B + '\n')
        self.report_path = self.run / 'runtime/report.json'
        data = gate.load(self.report_path)
        data.update(media_applied=3, media_deferred=2, disconnects=1, reconnects=1, syn_dropped=1)
        gate.write_json(self.report_path, data)
        self.startup = self.run / 'startup.log'
        with self.startup.open('a') as f:
            f.write(''.join(f'MEDIA_APPLIED generation={gen}\n' for gen in (A,B,A)))
        gate.refresh_sums(self.run)

    def tearDown(self):
        self.temp.cleanup()

    def save_actions(self):
        (self.session / 'actions.tsv').write_text(''.join(
            f'{STAMP}\t{name}\t{self.action_generations.get(name, "-")}\n' for name in self.events))

    def refresh_report(self, **changes):
        data = gate.load(self.report_path)
        data.update(changes)
        gate.write_json(self.report_path, data)
        gate.refresh_sums(self.run)

    def outcome(self):
        return review.inspect(self.bundle, self.session, self.run)

    def test_positive_synthetic_scope_only(self):
        r = self.outcome()
        self.assertEqual(r['exit_code'], 0)
        self.assertEqual(r['media']['state'], 'consistent')
        self.assertEqual(r['fault']['state'], 'consistent')
        self.assertFalse(r['board_accepted'])
        self.assertFalse(r['product_admitted'])
        self.assertTrue(r['human_review_required'])
        self.assertNotIn('reviewer', json.dumps(r))

    def test_missing_run_pending(self):
        result = review.inspect(self.bundle, self.session, self.root / 'absent')
        self.assertEqual(result['exit_code'], 2)
        self.assertEqual(result['runtime']['state'], 'pending')

    def test_missing_media_phase_pending(self):
        self.events.remove('B_VISIBLE_OPERATOR')
        self.save_actions()
        self.assertEqual(self.outcome()['media']['state'], 'rejected')

    def test_partial_media_phase_pending(self):
        self.events = list(review.MEDIA_SEQUENCE[:3])
        self.save_actions()
        self.assertEqual(self.outcome()['media']['state'], 'pending')

    def test_missing_fault_phase_pending(self):
        self.events.remove('SYN_DROPPED_OPERATOR')
        self.events = [x for x in self.events if x not in ('NO_GHOST_TAP_OPERATOR',)]
        self.save_actions()
        self.assertEqual(self.outcome()['fault']['state'], 'pending')

    def test_media_no_busy_defer_rejected(self):
        self.refresh_report(media_deferred=0)
        self.assertEqual(self.outcome()['media']['state'], 'rejected')

    def test_media_no_b_apply_rejected(self):
        self.startup.write_text(self.startup.read_text().replace(f'MEDIA_APPLIED generation={B}\n', ''))
        self.refresh_report(media_applied=2)
        self.assertEqual(self.outcome()['media']['state'], 'rejected')

    def test_media_application_counter_tamper_rejected(self):
        self.refresh_report(media_applied=9)
        self.assertEqual(self.outcome()['media']['state'], 'rejected')

    def test_media_wrong_rollback_rejected(self):
        (self.session / 'store/current').write_text(B + '\n' + A + '\n')
        self.assertEqual(self.outcome()['media']['state'], 'rejected')

    def test_media_signed_receipt_mismatch_rejected(self):
        p = self.session / 'install-b.out'
        p.write_text(p.read_text().replace('version=demo-b', 'version=demo-a'))
        self.assertEqual(self.outcome()['media']['state'], 'rejected')

    def test_media_same_generations_rejected(self):
        self.action_generations['B_BUSY_REQUESTED'] = A
        self.save_actions()
        self.assertEqual(self.outcome()['media']['state'], 'rejected')

    def test_fault_missing_counters_rejected(self):
        self.refresh_report(syn_dropped=0)
        self.assertEqual(self.outcome()['fault']['state'], 'rejected')
        self.assertFalse(self.outcome()['board_accepted'])

    def test_fault_only_reconnect_missing_rejected(self):
        self.refresh_report(reconnects=0)
        self.assertEqual(self.outcome()['fault']['state'], 'rejected')

    def test_headless_rejected(self):
        self.refresh_report(physical_io=False)
        self.assertEqual(self.outcome()['runtime']['state'], 'rejected')

    def test_synthetic_replay_rejected(self):
        self.refresh_report(synthetic_input=True)
        self.assertEqual(self.outcome()['runtime']['state'], 'rejected')

    def test_wrong_source_commit_rejected(self):
        self.refresh_report(commit='c'*40)
        self.assertEqual(self.outcome()['runtime']['state'], 'rejected')

    def test_mutated_original_report_checksum_rejected(self):
        self.report_path.write_text(self.report_path.read_text() + ' ')
        self.assertEqual(self.outcome()['runtime']['state'], 'rejected')

    def test_bad_approval_markers_rejected(self):
        self.events.insert(0, 'NO_GHOST_TAP_OPERATOR')
        self.save_actions()
        with self.assertRaises(gate.EvidenceError):
            self.outcome()

    def test_sensitive_text_in_markers_rejected(self):
        p = self.session / 'actions.tsv'
        p.write_text(p.read_text() + f'{STAMP}\ttyped_password\tsecret\n')
        with self.assertRaises(gate.EvidenceError):
            self.outcome()

    def test_symlinked_receipt_rejected(self):
        p = self.session / 'install-a.out'
        old = p.read_bytes()
        p.unlink()
        (self.root / 'target').write_bytes(old)
        p.symlink_to(self.root / 'target')
        self.assertEqual(self.outcome()['media']['state'], 'rejected')

    def test_session_wrong_bundle_rejected(self):
        (self.bundle / 'updates/demo-a.zip').write_bytes(b'CHANGED')
        with self.assertRaises(gate.EvidenceError):
            self.outcome()

    def test_output_protects_original_files(self):
        with redirect_stderr(StringIO()), redirect_stdout(StringIO()):
            result = review.main(['--bundle', str(self.bundle), '--session', str(self.session),
                                  '--run', str(self.run), '--output', str(self.session / 'out.json')])
        self.assertEqual(result, 1)
        self.assertFalse((self.session / 'out.json').exists())
        output = self.root / 'result.json'
        with redirect_stderr(StringIO()), redirect_stdout(StringIO()):
            self.assertEqual(review.main(['--bundle', str(self.bundle), '--session', str(self.session),
                                          '--run', str(self.run), '--output', str(output)]), 0)
            self.assertEqual(review.main(['--bundle', str(self.bundle), '--session', str(self.session),
                                          '--run', str(self.run), '--output', str(output)]), 1)
        self.assertFalse(json.loads(output.read_text())['board_accepted'])

    def test_device_helper_does_not_inject_faults(self):
        from subprocess import run
        helper = ROOT / 'scripts/device/r6-media-fault-lab.sh'
        self.assertEqual(run(['sh', '-n', str(helper)], check=False).returncode, 0)
        script = helper.read_text()
        self.assertNotIn('echo 1 > /sys/', script)
        self.assertNotIn('modprobe -r', script)
        self.assertNotIn('rmmod ', script)
        self.assertNotIn('EVIOCS', script)
        self.assertIn('R6_APPROVED_FAULT_SOP', script)


if __name__ == '__main__':
    unittest.main()
