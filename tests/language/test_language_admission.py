"""Declaration/path/evidence guard tests; fixtures are NOT Runtime acceptance."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import language_admission as a
import language_evidence as e

class LanguageGuards(unittest.TestCase):
    def setUp(self):
        self.p,self.m,self.o=a.inputs(ROOT)
    def reject(self,mutation):
        p,m,o=copy.deepcopy((self.p,self.m,self.o))
        mutation(p,m,o)
        with self.assertRaises((ValueError,KeyError,TypeError)):
            a.validate_claims(m,o,p,ROOT)
    def test_original_claims(self):
        a.validate_claims(self.m,self.o,self.p,ROOT)
    def test_no_software_admission_without_evidence(self):
        with self.assertRaises(ValueError):a.build_report(self.p,self.m,self.o,None,{},'0'*40,'unit')
    def test_missing_locale(self):self.reject(lambda p,m,o:m['rows'].pop())
    def test_duplicate_locale(self):self.reject(lambda p,m,o:m['rows'].__setitem__(1,copy.deepcopy(m['rows'][0])))
    def test_unknown_claim(self):self.reject(lambda p,m,o:m['rows'][0].update({'production_passed':True}))
    def test_product_admission(self):self.reject(lambda p,m,o:m['rows'][0].update({'product_input_admitted':True}))
    def test_600_fabricated(self):self.reject(lambda p,m,o:m['rows'][1].update({'physical_600':'passed'}))
    def test_800_fabricated(self):self.reject(lambda p,m,o:m['rows'][1].update({'physical_800':'passed'}))
    def test_english_false_ime(self):self.reject(lambda p,m,o:m['rows'][0].update({'ime_status':'implemented'}))
    def test_chinese_false_not_applicable(self):self.reject(lambda p,m,o:m['rows'][1].update({'ime_status':'not-required'}))
    def test_latin_ime_not_required_is_not_failure(self):self.assertEqual(self.m['rows'][2]['ime_status'],'not-required')
    def test_latin_buffer_not_input(self):self.reject(lambda p,m,o:m['rows'][2].update({'native_text_status':'rendered'}))
    def test_chinese_general_unicode(self):self.reject(lambda p,m,o:m['rows'][1].update({'font_shaping_status':'all-Unicode'}))
    def test_english_accents(self):self.reject(lambda p,m,o:m['rows'][0].update({'font_shaping_status':'Latin-all'}))
    def test_missing_software_evidence(self):self.reject(lambda p,m,o:m['rows'][1].update({'evidence':[]}))
    def test_nonexistent_evidence(self):self.reject(lambda p,m,o:m['rows'][0].update({'evidence':['missing-file.c']}))
    def test_hardware_keyboard_false_claim(self):self.reject(lambda p,m,o:m.update({'hardware_keyboard':'passed'}))
    def test_privacy(self):self.reject(lambda p,m,o:m['sensitive_policy'].update({'log_text':True}))
    def test_learning(self):self.reject(lambda p,m,o:o['provider'].update({'learning':True}))
    def test_network(self):self.reject(lambda p,m,o:o['provider'].update({'network':True}))
    def test_ui_input_conflation(self):self.reject(lambda p,m,o:m.update({'independent_settings':['locale']}))
    def test_ui_extra_selectable_locale(self):self.reject(lambda p,m,o:o['input_locales'].append('fr-FR'))
    def test_missing_keyboard_capability(self):self.reject(lambda p,m,o:o['required_capabilities'].remove('ui.keyboard.ascii'))
    def test_missing_dictionary_resource(self):self.reject(lambda p,m,o:o['resources'].remove('pinyin.dat'))
    def test_small_test_word_inventory(self):self.reject(lambda p,m,o:o.update({'font_inventory_glyphs':4}))
    def test_fabricated_font_budget(self):self.reject(lambda p,m,o:o.update({'font_max_bytes':999999999}))
    def test_field_restrictions_missing(self):self.reject(lambda p,m,o:m['rows'][0].update({'restrictions':''}))
    def test_new_language_not_silently_admitted(self):self.reject(lambda p,m,o:p['software_candidates'].append('ja-JP'))
    def test_duplicate_candidate(self):self.reject(lambda p,m,o:p['software_candidates'].append('en-US'))
    def test_missing_followup(self):self.reject(lambda p,m,o:p['deferred'].pop('ja-JP'))
    def test_parent_is_not_independent_followup(self):self.reject(lambda p,m,o:p['deferred'].update({'ja-JP':12}))
    def test_market_not_approved(self):self.reject(lambda p,m,o:p.update({'market_confirmation':'approved'}))
    def test_production_scope(self):self.reject(lambda p,m,o:p.update({'scope':'production'}))
    def test_missing_artifact_digest(self):self.reject(lambda p,m,o:p['baseline']['framework_artifact'].pop('sha256'))
    def test_malformed_source_commit(self):self.reject(lambda p,m,o:p['baseline'].update({'commit':'main'}))
    def test_unapproved_repo(self):self.reject(lambda p,m,o:p['baseline'].update({'repository':'other/repo'}))
    def test_stale_source_report(self):
        with self.assertRaises(ValueError):e.check_source_report({'commit':'old','physical_hardware':False},{'commit':'new'})
    def test_qemu_is_not_physical(self):
        with self.assertRaises(ValueError):e.check_source_report({'commit':'same','physical_hardware':True},{'commit':'same'})
    def test_malformed_json_duplicate(self):
        with self.assertRaises(ValueError):json.loads('{"passed":false,"passed":true}',object_pairs_hook=e.unique)
    def test_paths_reject_escape(self):
        with tempfile.TemporaryDirectory() as d:
            for path in ('../file','/tmp/file','a/../b','a//b','./b','x\\b','.'):
                with self.subTest(path=path),self.assertRaises(ValueError):e.member(Path(d),path)
    def test_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'link').symlink_to('/tmp')
            with self.assertRaises(ValueError):e.member(root,'link/file')
    def test_hash_tamper_and_missing(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'report').write_bytes(b'original')
            h=hashlib.sha256(b'original').hexdigest();e.check_hash(root,'report',h)
            (root/'report').write_bytes(b'changed')
            with self.assertRaises(ValueError):e.check_hash(root,'report',h)
            with self.assertRaises(ValueError):e.check_hash(root,'missing',h)
    def test_raw_manifest_cannot_hide_extra_evidence(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);folder=root/'case';folder.mkdir();(folder/'a').write_bytes(b'ok')
            record={'directory':'case','evidence':{'a':e.sha(folder/'a')}}
            e.check_record_files(root,record)
            (folder/'extra').write_bytes(b'not-indexed')
            with self.assertRaises(ValueError):e.check_record_files(root,record)
    def test_missing_independent_rerun(self):
        with self.assertRaises(ValueError):a.audit_rerun({},ROOT)

if __name__=='__main__':unittest.main()
