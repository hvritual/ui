"""Real rendered offline-input and production-package proofs. Never physical admission."""
from __future__ import annotations
import json
from pathlib import Path
import shutil
import struct
import hashlib
import framework as f
import input_assets
import pui
from application_acceptance import files

ROOT, OUT = f.ROOT, f.OUT


def input_trace(height: int, *, close: bool = True) -> str:
    """Independent fixed-coordinate synthetic user trace, not native key commands.

    Pace this trace against monotonic time so the real decoder child can run.
    Timing is a generous correctness fixture, not an IME performance benchmark.
    """
    rows = []; clock = 100
    def tap(x, y):
        nonlocal clock
        rows.append((clock,1,0,x,y)); clock += 40
        rows.append((clock,3,0,x,y)); clock += 40
    tap(675,height-36); tap(688,226)  # open actual Coffee editor, clear synthetic name
    tap(212,height-50); tap(512,height-122)  # language popup, Chinese
    # Weighted QWERTY rows; no public/native method bypass.
    for letter in 'nihao':
        if letter in 'qwertyuiop':
            x=16+'qwertyuiop'.index(letter)*100+46; y=height-230
        elif letter in 'asdfghjkl':
            x=16+'asdfghjkl'.index(letter)*111+51; y=height-170
        else:
            x=16+167+8+'zxcvbnm'.index(letter)*91+41; y=height-110
        tap(x,y)
    clock += 800; rows.append((clock,0,0,0,0)); clock += 40
    tap(164,height-292)  # choose genuine first candidate
    clock += 100; rows.append((clock,0,0,0,0)); clock += 40
    if close: tap(900,40)  # commit form, return to Coffee
    rows.append((clock+100,0,0,0,0))
    return ''.join(' '.join(map(str,r))+'\n' for r in rows)


def prove_input_binary(binary: Path, mode: str, directory: Path, *, static: bool = False) -> dict:
    directory.mkdir(parents=True, exist_ok=False)
    before=f.state(); runtime=f.sha(binary)
    owner=json.loads(binary.with_suffix(binary.suffix+'.ownership.json').read_text())
    if owner['consumer']!='runtime' or not owner['runtime_input_policy_passed'] or owner['binary_sha256']!=runtime:
        raise RuntimeError('input proof requires actual fixed production Runtime')
    if any(v['role'] in ('application','test-only') or v['reference_only'] for v in owner['inputs'].values()):
        raise RuntimeError('test/application source in production input executable')
    package=OUT/'packages/coffee-ime.pui'
    digest=f.sha(package); prefix=[*f.runner(mode,static),binary]; results={}; cases=0
    def execute(name,options,expected=0,error=None,no_output=False):
        nonlocal cases
        target=directory/name
        text=f.run([*prefix,*options,'--output',target],directory/(name+'.log'),expected,timeout=120)
        cases+=1
        if no_output:
            if target.exists() or (error and error not in text):raise RuntimeError('input admission failed open '+name)
            return None
        report=json.loads((target/'report.json').read_text())
        if report['commit']!=before['commit'] or report['ok']!=(expected==0) or report['physical_io'] or report['visual_validated'] or report['core_live_bytes_after_close']:
            raise RuntimeError('input production report invalid '+name)
        if error and report['error']!=error:raise RuntimeError('wrong input rejection '+name+': '+str(report['error']))
        # Metadata must not disclose committed/preedit/candidate contents.
        if any(word in text+(target/'report.json').read_text() for word in ('你好','nihao','R4_SYNTHETIC_SECRET')):
            raise RuntimeError('input diagnostics disclosed synthetic content '+name)
        return report
    for height in (600,800):
        for close in (True,False):
            name=f'input-{height}'+('-closed' if close else '-private')
            trace=directory/(name+'.trace');trace.write_text(input_trace(height,close=close))
            options=['--headless','--profile',f'imx6ul-1024x{height}','--package',package,'--allow-unsigned-package',
                     '--replay-input',trace,'--replay-realtime','--seconds','1']
            report=execute(name,options)
            expected={'package_admitted':True,'package_authenticated':False,'application_capabilities':15,
                'package_sha256':digest,'synthetic_input':True,'replay_realtime':True,'ime_commits':1,
                'text_input_open':not close,'text_input_confirms':1 if close else 0,'text_input_opens':1}
            if any(report[k]!=v for k,v in expected.items()) or report['ime_candidate_batches']<1:
                raise RuntimeError('real candidate/commit path missing '+name+' '+json.dumps(report))
            # R6 board failures need structured metadata; never typed-text diagnostics.
            metadata=('ime_provider_status','ime_provider_stage','ime_provider_errno','ime_provider_storage','ime_input_locale')
            if any(type(report.get(key)) is not int for key in metadata):
                raise RuntimeError('missing typed IME provider diagnostic '+name)
            if report['ime_provider_stage']!=0 or report['ime_provider_errno']!=0 or \
               report['ime_provider_storage'] not in (1,2) or report['ime_input_locale']!=2:
                raise RuntimeError('successful Chinese path has inconsistent IME diagnostic '+name)
            if (directory/name/'last.ppm').exists()!=close:
                raise RuntimeError('input screenshot suppression broken '+name)
            results[name]={**expected,'first':f.sha(directory/name/'first.ppm'),
                'last':f.sha(directory/name/'last.ppm') if close else None}
    base=['--headless','--profile','imx6ul-1024x600','--seconds','1']
    execute('unsigned-default',[*base,'--package',package],1,'PUI_UNSIGNED',True)
    execute('paced-without-trace',[*base,'--package',package,'--allow-unsigned-package','--replay-realtime'],2,no_output=True)
    trace=directory/'input-600-closed.trace'
    execute('physical-replay',['--physical','--profile','imx6ul-1024x600','--package',package,'--allow-unsigned-package',
        '--replay-input',trace,'--replay-realtime'],2,no_output=True)
    for name,offset,value in [('unknown-capability',32,31),('missing-keyboard-capability',32,13),('resource-without-capability',32,7)]:
        data=bytearray(package.read_bytes());struct.pack_into('<I',data,offset,value);data[160:192]=pui.container_digest(data)
        invalid=directory/(name+'.pui');invalid.write_bytes(data)
        execute(name,[*base,'--package',invalid,'--allow-unsigned-package'],1,'PUI_CAPABILITY',True)
    source=directory/'variant';shutil.copytree(OUT/'applications/coffee-ime',source)
    manifest=pui.load_manifest(OUT/'packages/coffee-ime.manifest.json')
    def variant(name):
        result=directory/(name+'.pui');result.write_bytes(pui.build(manifest,source));return result
    dictionary=(source/'pinyin.dat').read_bytes()
    corrupt=bytearray(dictionary);corrupt[-1]^=1;(source/'pinyin.dat').write_bytes(corrupt)
    execute('wrong-dictionary',[*base,'--package',variant('wrong-dictionary'),'--allow-unsigned-package'],1,'APP_OPEN')
    (source/'pinyin.dat').write_bytes(dictionary)
    atlas=(source/'input.atlas').read_bytes()
    for name,at in [('wrong-font-header',12),('partial-font-inventory',16)]:
        corrupt=bytearray(atlas);corrupt[at]^=1;(source/'input.atlas').write_bytes(corrupt)
        execute(name,[*base,'--package',variant(name),'--allow-unsigned-package'],1,'ENGINE_OPEN')
    (source/'input.atlas').write_bytes(atlas)
    original=(source/'application.js').read_text()
    # A private native scalar reference cannot be created by public application code.
    for name,code in {
        'private-glyph-ref':"__pocketCall('component.create',2,0,0,0,10,10,1,85536,0)",
        'unsupported-input-character':"__pocketCall('keyboard.begin',2,1,1,2,3,4,5,6,1);__pocketCall('keyboard.field',0,501,205,4,64,1,0,'\\u0627');__pocketCall('keyboard.languages',3,1);__pocketCall('keyboard.show')",
    }.items():
        (source/'application.js').write_text(original+'\nconst boot=PocketApplication.start;PocketApplication.start=function(c){boot(c);'+code+';return true;};')
        execute(name,[*base,'--package',variant(name),'--allow-unsigned-package'],1,'APP_OPEN')
    # Positive counterpart: identical valid commands with an admitted Han
    # initial value must succeed. The negative is not merely malformed API use.
    command="__pocketCall('keyboard.begin',2,1,1,2,3,4,5,6,1);__pocketCall('keyboard.field',0,501,205,4,64,1,0,'\\u4f60');__pocketCall('keyboard.languages',3,1);__pocketCall('keyboard.show')"
    (source/'application.js').write_text(original+'\nconst boot=PocketApplication.start;PocketApplication.start=function(c){boot(c);'+command+';return true;};')
    execute('admitted-input-character',[*base,'--package',variant('admitted-input-character'),'--allow-unsigned-package'])
    # No input resources are supplied to legacy applications, even though the
    # Runtime itself links the provider. The original Coffee/panel proofs remain.
    legacy=directory/'legacy';shutil.copytree(OUT/'assets',legacy)
    old=(legacy/'application.js').read_text()
    (legacy/'application.js').write_text(old+"\nconst boot=PocketApplication.start;PocketApplication.start=function(c){boot(c);__pocketCall('keyboard.languages',3,2);return true;};")
    m=pui.load_manifest(ROOT/'contracts/application-package.example.json')
    denied=directory/'legacy-capability.pui';denied.write_bytes(pui.build(m,legacy))
    execute('legacy-capability',[*base,'--package',denied,'--allow-unsigned-package'],1,'APP_OPEN')
    missing=bytearray(denied.read_bytes());struct.pack_into('<I',missing,32,15);missing[160:192]=pui.container_digest(missing)
    path=directory/'missing-input-resources.pui';path.write_bytes(missing)
    execute('missing-input-resources',[*base,'--package',path,'--allow-unsigned-package'],1,'PUI_REQUIRED_FILE',True)
    if f.sha(binary)!=runtime or f.sha(package)!=digest or before!=f.state():raise RuntimeError('input sources/Runtime changed during proof')
    result={**before,'runtime_sha256':runtime,'package_sha256':digest,'mode':mode,'static':static,'cases':cases,
        'rendered_ui':True,'genuine_offline_provider':True,'runtime_package_integration':True,
        'physical_hardware':False,'product_language_admitted':False,'qt_linked':False,
        'directory':directory.relative_to(OUT).as_posix(),'results':results,'evidence':files(directory)}
    print('INPUT_PRODUCTION_PROOF_OK',mode,'cases='+str(cases),'physical=false');return result


def test_input(mode: str, sanitize: bool=False) -> None:
    before=f.state()
    package=input_assets.build()
    atlas=json.loads((OUT/'input-atlas.json').read_text())
    if atlas['identity']['debug_only']:raise RuntimeError('debug font cannot pass input acceptance')
    directory=f.fresh(OUT/mode,'input-sanitize' if sanitize else 'input')
    binary=f.compile_binary(mode,test=True,sanitize=sanitize,package_test='input')
    outputs=[]
    for label in ('cases','repeat','negative'):
        dest=directory/label;dest.mkdir();negative=label=='negative'
        text=f.run([*f.runner(mode),binary,package,dest,OUT/'assets',*(['--intentional-failure'] if negative else [])],
            directory/(label+'.log'),1 if negative else 0,timeout=300)
        if negative:
            if 'INTENTIONAL_IME_VIEW_ASSERTION_FAILURE' not in text:raise RuntimeError('input deliberate-failure assertion missing')
        else:
            if text.count('IME_VIEW_OK')!=2 or 'INPUT_CHUNK_NEGATIVES_OK 7' not in text:raise RuntimeError('incomplete input visual cases')
            outputs.append(files(dest,lambda p:p.suffix=='.ppm'))
    if outputs[0]!=outputs[1] or len(outputs[0])!=28:raise RuntimeError('input images incomplete or nondeterministic '+str(len(outputs[0])))
    proof=None if sanitize else prove_input_binary(OUT/mode/'ui-framework',mode,directory/'production')
    if before!=f.state():raise RuntimeError('input source drift')
    f.write(OUT/mode/('input-sanitize.json' if sanitize else 'input.json'),{
        **before,'mode':mode,'sanitizer':sanitize,'physical_hardware':False,'product_language_admitted':False,
        'binary':binary.relative_to(OUT).as_posix(),'binary_sha256':f.sha(binary),
        'font_receipt_sha256':f.sha(OUT/'input-atlas.json'),'package_sha256':f.sha(package),
        'images':outputs[0],'production_proof':proof,'directory':directory.relative_to(OUT).as_posix(),'evidence':files(directory)})
    print('INPUT_VIEW_ACCEPTANCE_OK',mode,'sanitizer='+str(sanitize))


def verify_input(current: dict) -> None:
    atlas=json.loads((OUT/'input-atlas.json').read_text())
    if atlas['identity']['debug_only'] or atlas['glyphs']!=16561 or atlas['bytes']>13*1024*1024:
        raise RuntimeError('unadmitted input font source/coverage/budget')
    reports=[]
    for mode,san in (('native',False),('arm',False),('native',True)):
        path=OUT/mode/('input-sanitize.json' if san else 'input.json');r=json.loads(path.read_text())
        if any(r[k]!=v for k,v in current.items()) or r['mode']!=mode or r['sanitizer']!=san or r['physical_hardware'] or r['product_language_admitted']:
            raise RuntimeError('stale/overclaimed input report')
        if r['font_receipt_sha256']!=f.sha(OUT/'input-atlas.json') or r['package_sha256']!=f.sha(OUT/'packages/coffee-ime.pui') or f.sha(OUT/r['binary'])!=r['binary_sha256']:
            raise RuntimeError('input resource/binary binding changed')
        if files(OUT/r['directory'])!=r['evidence'] or len(r['images'])!=28:raise RuntimeError('input evidence incomplete')
        if not san:
            p=r['production_proof']
            if not p or p['runtime_sha256']!=f.sha(OUT/mode/'ui-framework') or p['cases']<15 or p['physical_hardware'] or not p['runtime_package_integration']:
                raise RuntimeError('actual input production proof missing')
        reports.append(r)
    for r in reports[1:]:
        if r['images']!=reports[0]['images']:raise RuntimeError('input pixels differ across architecture/sanitizer')
    if reports[0]['production_proof']['results']!=reports[1]['production_proof']['results']:
        raise RuntimeError('input Native/ARM production behavior or pixels differ')
    f.write(OUT/'input-verification.json',{**current,'status':'passed','rendered_ui':True,'genuine_offline_provider':True,
        'runtime_package_integration':True,'physical_hardware':False,'product_language_admitted':False,'qt_linked':False,
        'image_count':28,'package_sha256':f.sha(OUT/'packages/coffee-ime.pui'),
        'reports':{m+('-sanitize' if s else ''):f.sha(OUT/m/('input-sanitize.json' if s else 'input.json'))
            for m,s in (('native',False),('arm',False),('native',True))}})
    print('INPUT_VIEW_VERIFIED software-only')
