#!/usr/bin/env python3
"""Derive language support from ONE capability matrix and verified raw evidence.

`check` validates declarations only. `current` consumes the fresh canonical
Framework job. `accept` additionally checks an immutable historical artifact and
independent reruns. No mode promotes hardware, publisher or product admission.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from language_evidence import EvidenceError, need, load, member, sha, git, verify_bundle, check_hash
import text_input

ROOT=Path(__file__).resolve().parents[1]
LOCALES=('en-US','zh-CN','de-DE','fr-FR','es-ES','pt-PT','zh-TW','ja-JP','ko-KR','ar','th-TH')
CANDIDATES=('en-US','zh-CN')
NEW_FILES={
    'contracts/language-release.json','contracts/language-release.schema.json',
    'scripts/language_admission.py','scripts/language_evidence.py','scripts/language.mk',
    'tests/language/test_language_admission.py','docs/language-admission.md',
    '.github/workflows/language-admission.yml',
}
ADMISSION_STEP="""      - name: Verify per-language software evidence and generate support report
        run: |
          python3 -m unittest discover -s tests/language -v
          python3 scripts/language_admission.py current --framework out/framework --output out/framework/language-admission
"""

def exact(value, keys, code):
    need(type(value) is dict and set(value)==set(keys),code)

def digest_string(value, size):
    return type(value) is str and re.fullmatch('[0-9a-f]{'+str(size)+'}',value) is not None

def validate_policy(p):
    exact(p,{'schema_version','owner_issue','capability_source','software_candidates','market_confirmation','scope','baseline','deferred','handoff'},'release policy fields')
    need(type(p['schema_version']) is int and p['schema_version']==1 and p['owner_issue']==67,'release schema/owner')
    need(p['capability_source']=='assets/locales/input-capabilities.json','single capability source required')
    need(p['software_candidates']==list(CANDIDATES),'unreviewed/duplicate software locale scope')
    need(p['market_confirmation']=='pending' and p['scope']=='restricted-software-evidence-only','market/production scope is not approved')
    b=p['baseline']
    exact(b,{'repository','pr','commit','tree','run_id','framework_artifact','emulator_artifact','emulator_sha256'},'baseline fields')
    need(b['repository']=='hvritual/ui' and type(b['pr']) is int and b['pr']>0 and type(b['run_id']) is int and b['run_id']>0,'invalid evidence origin')
    need(digest_string(b['commit'],40) and digest_string(b['tree'],40) and digest_string(b['emulator_sha256'],64),'invalid source identity')
    for key in ('framework_artifact','emulator_artifact'):
        exact(b[key],{'id','sha256'},'artifact anchor fields')
        need(type(b[key]['id']) is int and b[key]['id']>0 and digest_string(b[key]['sha256'],64),'invalid artifact anchor')
    need(type(p['deferred']) is dict and set(p['deferred'])==set(LOCALES)-set(CANDIDATES),'deferred locales missing')
    need(all(type(v) is int and v>0 and v not in (12,67) for v in p['deferred'].values()),'independent follow-up issue required')
    need(p['handoff']=={'physical':49,'performance':7,'release_recovery':9,'reliability':10},'handoff scope drift')


def validate_claims(matrix, offline, policy, root=ROOT):
    validate_policy(policy)
    text_input.matrix_check(matrix)
    exact(matrix,{'schema_version','owner_issue','status','independent_settings','hardware_keyboard','unicode_version','rows','sensitive_policy','remaining','optional_input_contract'},'unknown capability claim')
    need(matrix['status']=='optional-pinyin-keyboard-software' and matrix['owner_issue']==12,'wrong capability baseline')
    need(matrix['independent_settings']==['ui_locale','input_locale','keyboard_layout'],'display/input/layout conflated')
    need(matrix['hardware_keyboard']=='not-implemented-not-required-for-touch-keyboard','unproven hardware keyboard')
    need(matrix['unicode_version']=='17.0.0','Unicode contract changed')
    need([r['locale'] for r in matrix['rows']]==list(LOCALES),'missing/duplicate/reordered locale')
    allowed={'locale','proposed_layout','engine','dictionary','direction','layout_status','ime_status','font_shaping_status','native_text_status','product_input_admitted','layout','evidence','restrictions','capability','engine_lock','physical_600','physical_800'}
    latin={'en-US','de-DE','fr-FR','es-ES','pt-PT'}
    for row in matrix['rows']:
        need(set(row)<=allowed,'unknown per-locale admission field')
        loc=row['locale']
        need(row['product_input_admitted'] is False,'product input not approved')
        for key in ('physical_600','physical_800'):
            need(row.get(key,'pending')=='pending','physical proof cannot be inferred from software')
        need(row['direction']==('rtl' if loc=='ar' else 'ltr'),'text direction declaration drift')
        need(row['ime_status']==('not-required' if loc in latin else 'optional-rendered-offline-software' if loc=='zh-CN' else 'not-implemented'),'IME not-applicable/support confusion')
        if loc=='en-US':
            need(row['engine']=='direct' and row['font_shaping_status']=='ascii-fixed-cell-software-validated' and row['native_text_status']=='ascii-field-software','ASCII support overstated')
        elif loc=='zh-CN':
            need(row['font_shaping_status']=='pinned-BMP-Han-inventory-fixed-cell; no-general-shaping-or-BiDi' and row['native_text_status']=='optional-BMP-Han-field-software','Han repertoire overstated')
        else:
            need(row['native_text_status']=='unicode-buffer-only' and row['font_shaping_status']=='not-validated-for-editable-text','buffer/fonts mistaken for input support')
            need(row['engine']==('direct' if loc in latin else 'pending-selection'),'unverified regional engine')
        if loc in CANDIDATES:
            need(type(row.get('restrictions')) is str and row['restrictions'] and row.get('evidence'),'missing limitations/evidence')
            for path in row['evidence']:
                need(member(root,path).is_file(),'missing declared test/source reference')
    need(offline['input_locales']==list(CANDIDATES) and offline['display_locale_independent'] is True,'UI selectable-language contract mismatch')
    need(offline['required_capabilities']==['ui.core','ui.keyboard.ascii','ui.ime.pinyin'] and offline['capability']=='ui.ime.pinyin','IME capability dependency drift')
    need(offline['resources']==['input.atlas','pinyin.dat','PINYIN-NOTICE.txt'],'IME immutable resources drift')
    need(offline['font_inventory_glyphs']==16561 and offline['font_max_bytes']==13631488 and offline['font_transfer_max_bytes']==1048576,'font resource scope drift')
    expected={'rendered_ui':True,'composition_bridge':True,'offline_ime':True,'runtime_package_integration':True,'physical_hardware':False,'product_language_admitted':False,'qt_linked':False,'production_admission':False}
    need(offline['admission']==expected,'optional software path overclaims admission')
    need(offline['provider']['network'] is False and offline['provider']['learning'] is False,'offline/privacy contract drift')
    need(not any(matrix['sensitive_policy'].values()),'sensitive policy must deny learning/logging/normalization/composition')


def inputs(root=ROOT):
    policy=load(root/'contracts/language-release.json')
    matrix=load(root/policy['capability_source'])
    offline=load(root/'contracts/offline-input.json')
    validate_claims(matrix,offline,policy,root)
    return policy,matrix,offline


def inherited_sources(root: Path, baseline: Path, evidence: dict):
    """R5 may add metadata/checks, not silently reuse stale runtime evidence."""
    expected=evidence['basis']['source_files']
    current=set(git(root,'ls-files').splitlines())
    need(current-set(expected)<=NEW_FILES,'unreviewed added runtime/build file')
    need(set(expected)<=current,'baseline source deleted')
    for path,digest in expected.items():
        actual=member(root,path)
        if path=='.github/workflows/coffee.yml':
            old=member(baseline,path).read_text()
            now=actual.read_text()
            need(now==old or (now.count(ADMISSION_STEP)==1 and now.replace(ADMISSION_STEP,'',1)==old),'existing workflow changed beyond added language gate')
        else:
            need(sha(actual)==digest,'stale evidence for changed source: '+path)


def audit_rerun(receipt: dict, framework: Path):
    r=receipt.get('independent_rerun')
    need(type(r) is dict and r.get('physical_hardware') is False and r.get('fresh_compile') is False,'missing/overclaimed independent review')
    need(r.get('original_native_view_and_sanitizer')=='positive-repeat-negative-passed','missing pinned-font view reruns')
    directory=framework/'language-independent-rerun'
    need(r.get('raw_logs'),'missing raw rerun logs')
    for path,digest in r['raw_logs'].items():
        check_hash(directory,path,digest)
    for mode in ('native','arm'):
        proof=load(directory/(mode+'-proof.json'))
        need(proof['commit']==receipt['basis']['commit'] and proof['tree']==receipt['basis']['tree'] and proof['development'] is False,'wrong rerun source')
        need(proof['runtime_sha256']==r[mode]['runtime_sha256'] and proof['package_sha256']==receipt['packages']['coffee-ime.pui']['sha256'] and proof['cases']==r[mode]['cases'] and proof['cases']>=18,'incomplete exact-binary rerun')
        from language_evidence import check_record_files
        check_record_files(framework,proof)


def build_report(policy,matrix,offline,receipt,display,checker_commit,validation_scope):
    need(receipt and receipt.get('status')=='verified' and receipt.get('software_only') is True,'no verified runtime evidence')
    need(receipt.get('physical_hardware') is False and receipt.get('product_language_admitted') is False and receipt.get('authenticated') is False,'software evidence is not product/hardware/authentication')
    rows=[]
    for row in matrix['rows']:
        loc=row['locale'];supported=loc in policy['software_candidates']
        refs=row.get('evidence',[])
        entry={'locale':loc,'software_status':'supported-with-restrictions' if supported else 'not-supported-this-release',
            'display':{'ui_catalog_present':loc in display,'status':'static-ui-catalog-only' if loc in display else 'not-delivered','input_support_implied':False},
            'keyboard':{'layout':row.get('layout',row['proposed_layout']),'status':row['layout_status'],'hardware_keyboard':'not-implemented'},
            'editing':{'status':row['native_text_status'],'input_widget_supported':supported},
            'ime':{'required':False if row['ime_status']=='not-required' else True,'status':row['ime_status'],'engine':row['engine'],'dictionary':row['dictionary']},
            'text_layout':{'status':row['font_shaping_status'],'direction':row['direction'],'general_shaping':False,'general_bidi':False,
                'simple_repertoire_only':supported},
            'targets':{t:{'software':'passed' if supported else 'not-supported','physical':'pending','physical_test_eligible':supported,
                'evidence_report':('native/input.json','arm/input.json','native/input-sanitize.json') if supported else []}
                for t in ('imx6ul-1024x600','imx6ul-1024x800')},
            'product_input_admitted':False,'production_admitted':False,
            'restrictions':row.get('restrictions','Regional keyboard, rendered editing and/or engine not delivered; Unicode buffer capability is not language support.'),
            'source_evidence':refs,'evidence_commit':receipt['basis']['commit'] if supported else None,
            'follow_up_issue':policy['deferred'].get(loc),
            'sensitive_fields':{'password':'printable-ASCII-only','pin':'ASCII-digits-only','composition':False,'language_chooser':False,'learning':False,'text_logging':False}}
        rows.append(entry)
    packages={}
    for name,p in receipt['packages'].items():
        caps=p['targets'][0]['capabilities']
        locales=list(CANDIDATES) if caps&8 else ['en-US'] if caps&2 else []
        need(not (caps&8) or name=='coffee-ime.pui','unreviewed IME application variant')
        packages[name]={'sha256':p['sha256'],'capability_mask':caps,'authorized_input_locales':locales,
            'application_input_entry_not_implied':True,'authenticated':False,
            'files':p['targets'][0]['files']}
    return {'schema_version':1,'owner_issue':67,'status':'software-language-evidence-complete',
        'validation_scope':validation_scope,'checker_commit':checker_commit,'evidence_commit':receipt['basis']['commit'],
        'evidence_tree':receipt['basis']['tree'],'market_confirmation':policy['market_confirmation'],
        'scope':policy['scope'],'capability_source':policy['capability_source'],
        'capability_sha256':hashlib.sha256(json.dumps(matrix,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
        'physical_hardware':False,'product_language_admitted':False,'production_admission':False,'authenticated':False,
        'software_candidates':policy['software_candidates'],'rows':rows,'packages':packages,
        'resources':{'input_font_receipt':receipt['font_receipt'],'dictionary_sha256':offline['dictionary_sha256'],
            'engine_lock':'toolchains/pinyin.lock.json','unicode_lock':'toolchains/text-input.lock.json'},
        'handoff':policy['handoff'],'note':'Software support is restricted to declared repertoire/field/package policy. R5 completion neither closes all of #12 nor approves markets, physical boards or production.'}


def markdown(report):
    out=['# 本版本语言能力与证据说明','',
         '> 自动生成：能力来源为 '+report['capability_source']+'，不是第二份手工状态表。',
         '', '**范围：受限软件验收，不是市场发布、实机或量产批准。**',
         '', 'Runtime 证据提交：`'+report['evidence_commit']+'`。校验器提交：`'+report['checker_commit']+'`。',
         '', '| 语言 | 软件输入 | 界面文案 | 键盘 | IME | 600 / 800 软件 | 600 / 800 实机 | 后续 |',
         '|---|---|---|---|---|---|---|---|']
    for r in report['rows']:
        status='受限支持' if r['software_status']=='supported-with-restrictions' else '本轮不支持'
        follow='#'+str(r['follow_up_issue']) if r['follow_up_issue'] else '#49（实机）'
        out.append('| '+ ' | '.join([r['locale'],status,'静态文案；不等于输入支持' if r['display']['ui_catalog_present'] else '未交付',r['keyboard']['layout'],r['ime']['status'],'通过 / 通过' if status=='受限支持' else '未支持 / 未支持','待验 / 待验',follow])+' |')
    out += ['', '## 限制', '', '英文仅可打印 ASCII；简体中文仅显式授权包中的离线拼音和已准入 BMP 汉字集合。不是任意 Unicode、重音、繁体输入方案、日/韩/RTL 或复杂文字支持。',
        '', 'Password 仅 ASCII，PIN/Number 仅 ASCII 数字；密码/PIN 无组合输入、语言选择、学习和明文日志。界面显示语言、输入语言和键盘布局独立。',
        '', '当前首发市场仍待确认；两个 viewport 的软件结果不替代任何一个实际主板的触摸、可视、资源恢复和人审。签名认证为 false，开发显式放行不是认证。JS 堆预算不等于整个进程或字形缓存预算。',
        '', '## 可追溯交付包', '', '| 包 | SHA256 | 授权输入语言 |', '|---|---|---|']
    for name,p in sorted(report['packages'].items()):
        out.append('| '+name+' | `'+p['sha256']+'` | '+', '.join(p['authorized_input_locales'])+' |')
    out += ['', '包权限不保证某个应用界面已经暴露输入入口。完整测试路径、字体/词库身份、限制及逐目标证据见同目录 admission.json 和 evidence.json。',
        '', '## 后续验收', '', '#49/R6：两机型原始报告、LCD/触摸、人审及故障恢复。#7/P5：冻结输入场景后的真实 CPU/RSS/时延。#9/P7：资源更新、签名、中断与回滚。#10/P8：长稳和故障注入。', '']
    return '\n'.join(out)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True)
    sub.add_parser('check')
    for name in ('accept','current'):
        q=sub.add_parser(name);q.add_argument('--framework',type=Path,required=True);q.add_argument('--output',type=Path,required=True)
        if name=='accept':q.add_argument('--evidence',type=Path,required=True);q.add_argument('--baseline',type=Path,required=True)
    a=parser.parse_args();policy,matrix,offline=inputs()
    if a.action=='check':
        print('LANGUAGE_DECLARATIONS_VALID rows=11 candidates=2 software_acceptance=not-claimed physical=false')
        return
    framework=a.framework.resolve()
    if a.action=='accept':
        receipt=load(a.evidence)
        need(receipt['artifacts']==policy['baseline'],'receipt does not match immutable anchor')
        archive=a.evidence.resolve().parent/'downloads/framework.zip'
        need(sha(archive)==policy['baseline']['framework_artifact']['sha256'],'immutable artifact missing/tampered')
        fresh=verify_bundle(a.baseline.resolve(),framework,policy['baseline'])
        need({k:v for k,v in receipt.items() if k!='independent_rerun'}==fresh,'receipt differs from checked raw artifact')
        inherited_sources(ROOT,a.baseline.resolve(),receipt)
        audit_rerun(receipt,framework)
        scope='historical-runtime-reverified-identical-runtime-sources'
    else:
        baseline={'repository':'hvritual/ui','commit':git(ROOT,'rev-parse','HEAD'),'tree':git(ROOT,'rev-parse','HEAD^{tree}'),'run_id':os.environ.get('GITHUB_RUN_ID')}
        receipt=verify_bundle(ROOT,framework,baseline)
        scope='fresh-canonical-framework-evidence-not-an-independent-rerun'
    report=build_report(policy,matrix,offline,receipt,load(ROOT/'apps/coffee-demo/locales.json'),git(ROOT,'rev-parse','HEAD'),scope)
    report['resources']['engine_lock_sha256']=sha(ROOT/'toolchains/pinyin.lock.json')
    report['resources']['unicode_lock_sha256']=sha(ROOT/'toolchains/text-input.lock.json')
    a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'admission.json').write_text(json.dumps(report,ensure_ascii=False,indent=2,sort_keys=True)+'\n')
    (a.output/'support.md').write_text(markdown(report))
    (a.output/'evidence.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2,sort_keys=True)+'\n')
    print('LANGUAGE_ADMISSION_OK '+json.dumps({'locales':len(report['rows']),'software':report['software_candidates'],'physical':False,'product':False,'scope':scope},sort_keys=True))
    print(markdown(report))

if __name__=='__main__':
    try:
        main()
    except (OSError,ValueError,KeyError,TypeError) as exc:
        print('LANGUAGE_ADMISSION_FAILED',str(exc),file=sys.stderr)
        sys.exit(1)
