#!/usr/bin/env python3
"""R2 application equivalence and production-ELF reuse; never a physical gate."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import shutil

import framework as f

ROOT=f.ROOT
OUT=f.OUT
BASELINE='ba311b547aebec9d457829b748f2f0a660172467'
FROZEN = {'tests/framework/reference_framework.inc': '3ebae7101e6a8cd7801bdc4f73d12a907188d95aca2069472374982188c3a489', 'tests/framework/reference_keyboard.inc': 'b88b37d516733eff0db13a6fc669d264c6de559a61f65db533dd496c9f5d5276', 'tests/framework/reference_scene_wire.inc': 'a78133e41dc0abbd89d43d96509fdeaa912795be97f713e915a7a3314c783b82', 'tests/application/reference/hosts/linux/framework.c': 'bc4a85cbfd69f6b0f629aba7123538ecbea886d87d5817d9d12e67f3f25cd65f', 'tests/application/reference/hosts/linux/framework.h': '768263b48d53430bdfd2eb6948f6117ab0d40968756c63d2fcafe6c3358d2502', 'apps/coffee-framework/app.c': '5a0820cd4ca636004ad2a9de268f6f9781c3b95e2e66a01f92ee770a3728a1ee', 'apps/coffee-framework/app.h': '272e3a6f453dd910ef632751619b9824a33ea8bf5e7da1dd90a510eca62b3356', 'apps/coffee-framework/pager.c': '1935cb13fa947a99669fdc734288d19a6ab6f9da01904cc28c93ebfd3536742b', 'apps/coffee-framework/pager.h': 'e8e600328b96675befe395c55fb0a7383b969e008b0eb11cc58579cc9d520731'}


def source_guard():
    for name,expected in FROZEN.items():
        if f.sha(ROOT/name)!=expected:
            raise RuntimeError('reference source changed: '+name)


def files(directory, predicate=lambda p: True):
    return {p.relative_to(directory).as_posix():f.sha(p) for p in sorted(directory.rglob('*')) if p.is_file() and predicate(p)}


def legacy_outputs(directory):
    return files(directory,lambda p:p.suffix=='.ppm' or (p.name.startswith('replay-') and p.suffix=='.txt'))


def trace(application,height):
    rows=[];time=100
    def tap(x,y):
        nonlocal time
        rows.append((time,1,0,x,y));time+=40
        rows.append((time,3,0,x,y));time+=40
    if application=='coffee':
        tap(100,200);tap(640,height-112);tap(640,364)
        time+=5100;rows.append((time,0,0,0,0));time+=80
        tap(500,height-91)
    else:
        tap(180,height-124);tap(180,height-124);tap(500,height-124)
        tap(820,height-124);tap(780,height-124)
        tap(230,height-124)  # Capturing modal must prevent background Back.
        tap(340,348);tap(780,height-124);tap(650,348);tap(180,height-124)
    rows.append((time+100,0,0,0,0))
    return ''.join(' '.join(map(str,row))+'\n' for row in rows),len(rows)


def prove_same_binary(binary,mode,directory,static=False):
    directory.mkdir(parents=True,exist_ok=False)
    before=f.state();runtime_sha=f.sha(binary)
    ownership=json.loads(binary.with_suffix(binary.suffix+'.ownership.json').read_text())
    if ownership['consumer']!='runtime' or not ownership['runtime_input_policy_passed'] or ownership['binary_sha256']!=runtime_sha:
        raise RuntimeError('production ELF lacks fixed-runtime ownership proof')
    if any(v['role'] in ('application','test-only') or v['reference_only'] for v in ownership['inputs'].values()):
        raise RuntimeError('application/reference input in production ELF')
    prefix=[*f.runner(mode,static),binary]
    roots={'coffee':OUT/'assets','control-panel':OUT/'applications/control-panel'}
    results={}
    for application,assets in roots.items():
        for height in (600,800):
            name=f'{application}-{height}';payloads=files(assets)
            data,count=trace(application,height);replay=directory/(name+'.trace');replay.write_text(data)
            destination=directory/name
            f.run([*prefix,'--headless','--profile',f'imx6ul-1024x{height}','--asset-root',assets,
                   '--replay-input',replay,'--seconds','1','--output',destination],directory/(name+'.log'),timeout=180)
            r=json.loads((destination/'report.json').read_text())
            expected={'page_mask':15 if application=='coffee' else 3,'completed':1,'application_page':1,
                      'application_selection':0 if application=='coffee' else 1,'replay_samples':count}
            if not r['ok'] or not r['synthetic_input'] or r['physical_io'] or r['visual_validated'] or r['core_live_bytes_after_close']:
                raise RuntimeError('application replay scope/result invalid: '+name)
            if r['commit']!=before['commit'] or any(r[k]!=v for k,v in expected.items()):
                raise RuntimeError('incomplete independent application flow: '+name)
            if files(assets)!=payloads or f.sha(binary)!=runtime_sha:raise RuntimeError('payload/runtime changed during proof')
            results[name]={'runtime_sha256':runtime_sha,'application_files':payloads,'expected':expected,
                'first':f.sha(destination/'first.ppm'),'last':f.sha(destination/'last.ppm'),
                'report_sha256':f.sha(destination/'report.json')}
    for height in (600,800):
        if results[f'coffee-{height}']['first']==results[f'control-panel-{height}']['first']:
            raise RuntimeError('second application renders the same initial UI')
        if results[f'control-panel-{height}']['first']==results[f'control-panel-{height}']['last']:
            raise RuntimeError('second application did not visibly change state')
    # Modify application behavior only; no compilation or link is performed.
    variant=directory/'control-variant';shutil.copytree(roots['control-panel'],variant)
    original=(variant/'application.js').read_text()
    needle='Math.min(99,count+1)'
    if original.count(needle)!=1:raise RuntimeError('counter behavior fixture drift')
    (variant/'application.js').write_text(original.replace(needle,'Math.min(99,count+2)'))
    # This legacy asset must never be executed by the fixed adapter.
    (variant/'framework.js').write_text('throw Error("UNTRUSTED_ADAPTER_MUST_NOT_RUN");\n')
    data,count=trace('control-panel',600);replay=directory/'variant.trace';replay.write_text(data)
    destination=directory/'variant-run'
    f.run([*prefix,'--headless','--profile','imx6ul-1024x600','--asset-root',variant,'--replay-input',replay,
           '--seconds','1','--output',destination],directory/'variant.log',timeout=180)
    r=json.loads((destination/'report.json').read_text())
    if not r['ok'] or r['application_selection']!=2 or r['completed']!=1 or r['page_mask']!=3 or r['replay_samples']!=count:
        raise RuntimeError('application-only behavioral change did not take effect')
    if f.sha(destination/'last.ppm')==results['control-panel-600']['last'] or f.sha(binary)!=runtime_sha:
        raise RuntimeError('application-only update did not preserve Runtime independence')
    # Invalid replay input rejects; no source/clock/pixel oracle is relaxed.
    negative=directory/'invalid.trace';negative.write_text('100 3 0 100 100\n')
    bad=directory/'invalid-run'
    f.run([*prefix,'--headless','--profile','imx6ul-1024x600','--asset-root',roots['coffee'],
           '--replay-input',negative,'--output',bad],directory/'invalid.log',expected=1)
    r=json.loads((bad/'report.json').read_text())
    if r['ok'] or r['error']!='REPLAY_REJECTED' or r['physical_io']:raise RuntimeError('invalid replay admitted')
    # Replay is never permitted to drive an actual framebuffer.
    physical=directory/'forbidden-physical'
    f.run([*prefix,'--physical','--profile','imx6ul-1024x600','--asset-root',roots['coffee'],
           '--replay-input',replay,'--output',physical],directory/'physical-rejected.log',expected=2)
    if physical.exists():raise RuntimeError('physical replay performed output/device admission')
    # Both catalog syntax and application code are required. A data payload
    # cannot inject a second private JS adapter through executable concatenation.
    corrupt=directory/'corrupt';shutil.copytree(roots['control-panel'],corrupt)
    (corrupt/'catalog.json').write_text('[];throw Error("CATALOG_INJECTION")')
    failed=directory/'corrupt-run'
    f.run([*prefix,'--headless','--profile','imx6ul-1024x600','--asset-root',corrupt,'--seconds','1',
           '--output',failed],directory/'corrupt.log',expected=1)
    if json.loads((failed/'report.json').read_text())['ok']:raise RuntimeError('executable catalog admitted')
    if before!=f.state() or f.sha(binary)!=runtime_sha:raise RuntimeError('source or ELF changed during same-binary proof')
    proof={**before,'scope':'production-elf-synthetic-input','physical_hardware':False,'pui_admission':False,
           'mode':mode,'static':static,'runtime_sha256':runtime_sha,'same_binary_interactive_apps':True,
           'ownership_sha256':f.sha(binary.with_suffix(binary.suffix+'.ownership.json')),'runs':results,
           'application_only_change':{'application_sha256':f.sha(variant/'application.js'),'selection':2,
              'last_sha256':f.sha(destination/'last.ppm'),'runtime_sha256':runtime_sha},
           'evidence_root':directory.relative_to(OUT).as_posix(),'evidence':files(directory)}
    return proof


def test_applications(mode,sanitize,directory,current_cases):
    source_guard();before=f.state();d=directory/'independent-applications';d.mkdir()
    f.run(['python3',ROOT/'tests/application/test_system_include_guard.py'],d/'include-policy.log')
    binary=f.compile_binary(mode,test=True,sanitize=sanitize,application_test=True)
    outputs=[]
    for name in ('cases','repeat','negative'):
        output=d/name;output.mkdir()
        text=f.run([*f.runner(mode),binary,OUT/'assets',OUT/'applications/control-panel',output,
                    *(['--intentional-failure'] if name=='negative' else [])],d/(name+'.log'),expected=1 if name=='negative' else 0)
        if name=='negative':
            if 'INTENTIONAL_APPLICATION_ASSERTION_FAILURE' not in text or 'APPLICATIONS_OK' in text:
                raise RuntimeError('application negative assertion failed open')
        else:
            if 'APPLICATIONS_OK real-quickjs real-core two-interactive-applications' not in text:raise RuntimeError('missing full application suite')
            images=files(output,lambda p:p.suffix=='.ppm')
            if len(images)!=14:raise RuntimeError('incomplete two-application dual-viewport image matrix')
            outputs.append(images)
    if outputs[0]!=outputs[1]:raise RuntimeError('application image nondeterminism')
    reference=None
    if mode=='native' and not sanitize:
        ref=f.compile_binary('native',test=True,reference=True)
        rd=d/'native-reference';rd.mkdir()
        f.run([ref,OUT/'assets',rd],d/'native-reference.log')
        old,new=legacy_outputs(rd),legacy_outputs(current_cases)
        if len(old)!=48 or old!=new:
            missing=sorted(set(old)^set(new));different=[n for n in old.keys()&new.keys() if old[n]!=new[n]]
            raise RuntimeError('frozen Coffee behavior/pixel mismatch: '+str(missing+different))
        reference={'baseline':BASELINE,'frozen_source':FROZEN,'outputs':old,'binary_sha256':f.sha(ref),
                   'reference_directory':rd.relative_to(OUT).as_posix(),'current_directory':current_cases.relative_to(OUT).as_posix()}
    proof=None
    if not sanitize:proof=prove_same_binary(OUT/mode/'ui-framework',mode,d/'production-proof')
    if before!=f.state():raise RuntimeError('application source changed during test')
    report={**before,'mode':mode,'sanitizer':sanitize,'physical_hardware':False,'pui_admission':False,
            'binary':binary.relative_to(OUT).as_posix(),'binary_sha256':f.sha(binary),'images':outputs[0],
            'assets_sha256':f.sha(OUT/'assets.json'),'directory':d.relative_to(OUT).as_posix(),
            'reference':reference,'production_proof':proof,'evidence':files(d)}
    f.write(OUT/mode/('applications-sanitize.json' if sanitize else 'applications.json'),report)
    print('APPLICATION_INTEGRATION_OK',mode,'sanitizer='+str(sanitize),'physical=false')


def verify_applications(current):
    source_guard();reports=[]
    for mode,sanitize in (('native',False),('arm',False),('native',True)):
        path=OUT/mode/('applications-sanitize.json' if sanitize else 'applications.json');r=json.loads(path.read_text())
        if any(r[k]!=v for k,v in current.items()) or r['mode']!=mode or r['sanitizer'] is not sanitize or r['physical_hardware'] or r['pui_admission']:
            raise RuntimeError('stale application source/scope')
        if f.sha(OUT/r['binary'])!=r['binary_sha256'] or f.sha(OUT/'assets.json')!=r['assets_sha256']:
            raise RuntimeError('application binary/asset drift')
        if files(OUT/r['directory'])!=r['evidence']:raise RuntimeError('application evidence changed')
        if not sanitize:
            proof=r['production_proof'];binary=OUT/mode/'ui-framework'
            if any(proof[k]!=v for k,v in current.items()) or proof['mode']!=mode or proof['static'] or not proof['same_binary_interactive_apps'] or f.sha(binary)!=proof['runtime_sha256'] or proof['physical_hardware'] or proof['pui_admission']:
                raise RuntimeError('missing same-ELF interactive application proof')
            if f.sha(binary.with_suffix(binary.suffix+'.ownership.json'))!=proof['ownership_sha256']:raise RuntimeError('production ownership evidence drift')
            if files(OUT/proof['evidence_root'])!=proof['evidence']:raise RuntimeError('production reuse evidence drift')
        reports.append(r)
    if any(r['images']!=reports[0]['images'] for r in reports):raise RuntimeError('cross-target application pixel mismatch')
    reference=reports[0]['reference']
    if not reference or reference['frozen_source']!=FROZEN or len(reference['outputs'])!=48:
        raise RuntimeError('missing complete baseline equivalence')
    for key in ('reference_directory','current_directory'):
        if legacy_outputs(OUT/reference[key])!=reference['outputs']:raise RuntimeError('reference equivalence evidence drift')
    for name,a in reports[0]['production_proof']['runs'].items():
        b=reports[1]['production_proof']['runs'][name]
        if any(a[k]!=b[k] for k in ('first','last','expected','application_files')):raise RuntimeError('native/ARM production application behavior differs')
    f.write(OUT/'application-verification.json',{'status':'passed',**current,
        'scope':'software-runtime-application-separation','physical_hardware':False,'pui_admission':False,
        'reference_baseline':BASELINE,'equivalent_images':42,'equivalent_replays':6,'new_application_images':14,
        'same_production_elf_interactive_applications':True,'reports':{
            mode+('-sanitize' if sanitize else ''):f.sha(OUT/mode/('applications-sanitize.json' if sanitize else 'applications.json'))
            for mode,sanitize in (('native',False),('arm',False),('native',True))}})
    print('APPLICATION_SEPARATION_VERIFIED same-ELF dual-app real-Core; physical=false pui=false')
