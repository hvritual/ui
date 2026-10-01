"""Declaration/path/evidence guard tests; fixtures are NOT Runtime acceptance."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
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
    def test_legacy_evidence_root_uses_full_hash_validation(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);folder=root/'case';folder.mkdir();(folder/'a').write_bytes(b'original')
            record={'evidence_root':'case','evidence':{'a':e.sha(folder/'a')}}
            e.check_record_files(root,record)
            (folder/'a').write_bytes(b'changed')
            with self.assertRaises(ValueError):e.check_record_files(root,record)
    def test_ambiguous_evidence_root_rejected(self):
        with self.assertRaises(ValueError):e.record_directory({'directory':'a','evidence_root':'a'})
    def test_missing_evidence_root_rejected(self):
        with self.assertRaises(ValueError):e.record_directory({'evidence':{}})
    def test_bad_evidence_root_type_rejected(self):
        with self.assertRaises(ValueError):e.record_directory({'directory':None})
    def test_extracted_bytes_remain_bound_to_zip(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);archive=root/'evidence.zip';dest=root/'unpacked'
            with zipfile.ZipFile(archive,'w') as z:z.writestr('result.json','{"status":"original"}')
            e.extract(archive,dest)
            self.assertEqual(e.verify_extracted(archive,dest),1)
            (dest/'result.json').write_text('{"status":"forged"}')
            with self.assertRaises(ValueError):e.verify_extracted(archive,dest)
    def test_zip_traversal_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);archive=root/'bad.zip'
            with zipfile.ZipFile(archive,'w') as z:z.writestr('../escape','bad')
            with self.assertRaises(ValueError):e.extract(archive,root/'out')
    def test_zip_duplicate_rejected(self):
        import warnings
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);archive=root/'bad.zip'
            with warnings.catch_warnings():
                warnings.simplefilter('ignore',UserWarning)
                with zipfile.ZipFile(archive,'w') as z:
                    z.writestr('same','one');z.writestr('same','two')
            with self.assertRaises(ValueError):e.extract(archive,root/'out')
    def test_schema_matches_policy_shape(self):
        schema=e.load(ROOT/'contracts/language-release.schema.json')
        self.assertFalse(schema['additionalProperties'])
        self.assertEqual(set(schema['required']),set(self.p))
        for key,spec in schema['properties'].items():
            if 'const' in spec:self.assertEqual(self.p[key],spec['const'])
        self.assertEqual(set(schema['properties']['deferred']['required']),set(self.p['deferred']))
    def formatting_fixture(self):
        # Synthetic formatting data only; accept/current still require raw files.
        return {'status':'verified','software_only':True,'physical_hardware':False,
            'product_language_admitted':False,'authenticated':False,
            'basis':{'commit':'0'*40,'tree':'1'*40},'font_receipt':{},'packages':{}}
    def test_report_and_markdown_are_one_projection(self):
        report=a.build_report(self.p,self.m,self.o,self.formatting_fixture(),{'de-DE':{}},'2'*40,'unit-formatting-only')
        self.assertEqual([r['locale'] for r in report['rows']],list(a.LOCALES))
        self.assertEqual(sum(r['software_status']=='supported-with-restrictions' for r in report['rows']),2)
        text=a.markdown(report)
        for r in report['rows']:
            self.assertIn('| '+r['locale']+' |',text)
            self.assertFalse(r['product_input_admitted'])
            self.assertFalse(r['production_admitted'])
            self.assertEqual({t['physical'] for t in r['targets'].values()},{'pending'})
        german=next(r for r in report['rows'] if r['locale']=='de-DE')
        self.assertTrue(german['display']['ui_catalog_present'])
        self.assertFalse(german['display']['input_support_implied'])
        self.assertEqual(german['software_status'],'not-supported-this-release')
        self.assertFalse(report['rows'][0]['ime']['required'])
        self.assertTrue(report['rows'][1]['ime']['required'])
        self.assertEqual(text,a.markdown(copy.deepcopy(report)))
    def test_report_rejects_fabricated_physical_receipt(self):
        receipt=self.formatting_fixture();receipt['physical_hardware']=True
        with self.assertRaises(ValueError):a.build_report(self.p,self.m,self.o,receipt,{},'2'*40,'unit')
    def test_image_paths_preserve_each_viewport(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);expected={}
            for height in (600,800):
                folder=root/('input-'+str(height));folder.mkdir()
                path=folder/'candidate.ppm';path.write_bytes(b'P6 fixture '+str(height).encode())
                expected[path.relative_to(root).as_posix()]=e.sha(path)
            self.assertEqual(e.image_manifest(root),expected)
            e.compare_images(root,expected)
            (root/'input-800/candidate.ppm').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'different'):e.compare_images(root,expected)
    def test_missing_viewport_and_extra_pixels_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'input-600').mkdir();path=root/'input-600/candidate.ppm';path.write_bytes(b'fixture')
            expected={'input-600/candidate.ppm':e.sha(path),'input-800/candidate.ppm':e.sha(path)}
            with self.assertRaisesRegex(ValueError,'missing'):e.compare_images(root,expected)
            with self.assertRaisesRegex(ValueError,'extra'):e.compare_images(root,{})
    def test_empty_pixel_proof_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError,'empty'):e.compare_images(Path(d),{})

if __name__=='__main__':unittest.main()
