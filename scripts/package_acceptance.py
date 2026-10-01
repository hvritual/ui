"""Verify actual packaged application execution; never hardware/authentication admission."""
from __future__ import annotations
import json
from pathlib import Path
import shutil
import struct

import framework as f
import pui
from application_acceptance import files, legacy_outputs, trace

ROOT, OUT = f.ROOT, f.OUT


def manifest(application: str) -> dict:
    m = pui.load_manifest(ROOT/'contracts/application-package.example.json')
    if application == 'control-panel':
        m['app_id'] = 'com.pocket.control-panel'
        m['capabilities'] = ['ui.core']
        m['budgets']['heap_bytes'] = 1024*1024
    return m


def build_packages(directory: Path) -> dict[str, Path]:
    directory.mkdir(parents=True, exist_ok=True)
    paths = {}
    for app, source in (('coffee', OUT/'assets'), ('control-panel', OUT/'applications/control-panel')):
        m = manifest(app)
        payload = pui.build(m, source)
        # Independent Python policy check is in addition to the C runtime path.
        for target in f.PROFILES:
            pui.verify(payload, target, allow_unsigned=True)
        paths[app] = directory/(app+'.pui')
        paths[app].write_bytes(payload)
        f.write(directory/(app+'.manifest.json'), m)
    return paths


def prove_package_binary(binary: Path, mode: str, directory: Path, static: bool = False) -> dict:
    directory.mkdir(parents=True, exist_ok=False)
    before = f.state()
    runtime_sha = f.sha(binary)
    ownership = json.loads(binary.with_suffix(binary.suffix+'.ownership.json').read_text())
    if ownership['consumer'] != 'runtime' or not ownership['runtime_input_policy_passed'] or ownership['binary_sha256'] != runtime_sha:
        raise RuntimeError('package proof must use actual production ELF')
    if any(v['role'] in ('application', 'test-only') or v['reference_only'] for v in ownership['inputs'].values()):
        raise RuntimeError('test/application input in packaged production runtime')
    for name in ('hosts/linux/package/package.c', 'hosts/linux/package/loaded.c'):
        if name not in ownership['inputs']:
            raise RuntimeError('package parser/immutable loader absent from real ELF')
    packages = build_packages(directory/'packages')
    prefix = [*f.runner(mode, static), binary]
    results = {}
    count_cases = 0

    def run_case(name, options, expected=0, error=None, no_output=False):
        nonlocal count_cases
        destination = directory/name
        text = f.run([*prefix, *options, '--output', destination], directory/(name+'.log'), expected=expected, timeout=180)
        count_cases += 1
        if no_output:
            if destination.exists() or (error and error not in text):
                raise RuntimeError('package pre-admission failed open: '+name)
            return None
        r = json.loads((destination/'report.json').read_text())
        if r['commit'] != before['commit'] or r['ok'] != (expected == 0) or r['physical_io'] or r['visual_validated'] or r['core_live_bytes_after_close']:
            raise RuntimeError('invalid package runtime result: '+name)
        if error and r['error'] != error:
            raise RuntimeError('wrong failure path: '+name+' '+str(r['error']))
        if 'R3_SYNTHETIC_EXCEPTION_SECRET' in text or 'R3_SYNTHETIC_EXCEPTION_SECRET' in (destination/'report.json').read_text():
            raise RuntimeError('application exception content leaked')
        return r

    for app, package in packages.items():
        assets = OUT/'assets' if app == 'coffee' else OUT/'applications/control-panel'
        for height in (600, 800):
            name = f'{app}-{height}'
            data, count = trace(app, height)
            replay = directory/(name+'.trace'); replay.write_text(data)
            common = ['--headless', '--profile', f'imx6ul-1024x{height}', '--replay-input', replay, '--seconds', '1']
            baseline = run_case(name+'-directory', [*common, '--asset-root', assets])
            r = run_case(name, [*common, '--package', package, '--allow-unsigned-package'])
            fields = ['page_mask','completed','application_page','application_selection','replay_samples','text_input_opens','text_input_confirms','text_input_cancels']
            if any(r[k] != baseline[k] for k in fields) or r['replay_samples'] != count or r['completed'] != 1:
                raise RuntimeError('package application semantics changed: '+name)
            if not r['package_admitted'] or r['package_authenticated'] or r['package_sha256'] != f.sha(package):
                raise RuntimeError('package admission not bound to immutable input: '+name)
            m = manifest(app)
            cap = sum(pui.CAPABILITIES[c] for c in m['capabilities'])
            if r['application_heap_limit'] != m['budgets']['heap_bytes'] or r['application_capabilities'] != cap:
                raise RuntimeError('declared policy not applied to application: '+name)
            for image in ('first.ppm', 'last.ppm'):
                if f.sha(directory/name/image) != f.sha(directory/(name+'-directory')/image):
                    raise RuntimeError('directory/package pixel mismatch: '+name)
            results[name] = {k:r[k] for k in fields}
            results[name].update(first=f.sha(directory/name/'first.ppm'), last=f.sha(directory/name/'last.ppm'),
                package_sha256=f.sha(package), runtime_sha256=runtime_sha, heap_bytes=r['application_heap_limit'], capabilities=cap)
    for height in (600,800):
        if results[f'coffee-{height}']['first'] == results[f'control-panel-{height}']['first']:
            raise RuntimeError('not two different packaged applications')
    base = ['--headless', '--profile', 'imx6ul-1024x600', '--seconds', '1']
    run_case('unsigned-default', [*base, '--package', packages['coffee']], 1, 'PUI_UNSIGNED', True)
    run_case('ambiguous-input', [*base, '--package', packages['coffee'], '--asset-root', OUT/'assets', '--allow-unsigned-package'], 2, no_output=True)
    run_case('flag-without-package', [*base, '--asset-root', OUT/'assets', '--allow-unsigned-package'], 2, no_output=True)
    replay = directory/'control-panel-600.trace'
    run_case('physical-replay', ['--physical','--profile','imx6ul-1024x600','--package',packages['coffee'],
        '--allow-unsigned-package','--replay-input',replay], 2, no_output=True)
    tampered = directory/'tampered.pui'; payload = bytearray(packages['coffee'].read_bytes()); payload[-1] ^= 1; tampered.write_bytes(payload)
    run_case('tamper-before-device', ['--physical','--profile','imx6ul-1024x600','--fbdev','/dev/null',
        '--package',tampered,'--allow-unsigned-package'], 1, 'PUI_INTEGRITY', True)
    for name, offset, value, error in [('runtime-version',16,2,'PUI_VERSION'),('sdk-version',24,2,'PUI_VERSION'),
                                      ('target',28,2,'PUI_TARGET'),('budget',48,64*1024*1024,'PUI_BUDGET'),
                                      ('unknown-capability',32,15,'PUI_CAPABILITY'),('future-auth',60,1,'PUI_FORMAT')]:
        payload = bytearray(packages['coffee'].read_bytes()); struct.pack_into('<I',payload,offset,value)
        payload[160:192] = pui.container_digest(payload); invalid = directory/(name+'.pui'); invalid.write_bytes(payload)
        run_case(name, [*base,'--package',invalid,'--allow-unsigned-package'],1,error,True)

    # Validly hashed packages may still ask for a forbidden operation. Exercise
    # the actual native command path, including alternate resource entry points.
    variant = directory/'variant-source'; shutil.copytree(OUT/'applications/control-panel', variant)
    original = (variant/'application.js').read_text()
    def variant_package(name, source, m=None):
        (variant/'application.js').write_text(source)
        path = directory/(name+'.pui'); path.write_bytes(pui.build(m or manifest('control-panel'), variant)); return path
    bad_commands = {
        'image-command': "__pocketCall('component.image',1,1)",
        'image-component': "__pocketCall('component.create',3,0,0,0,10,10,1,0,0)",
        'image-property': "__pocketCall('component.create',1,0,0,0,10,10,1,0,1)",
        'image-list': "const c=__pocketCall('component.create',1,0,0,0,10,10,1,0,0);__pocketCall('list.create',c,1,1,3)",
        'image-binding': "const c=__pocketCall('component.create',1,0,0,0,10,10,1,0,0);const s=__pocketCall('signal.create',1);__pocketCall('signal.bind',s,c,3)",
        'keyboard-command': "__pocketCall('keyboard.step')",
        'keyboard-overlay': "const c=__pocketCall('component.create',16,0,0,0,1024,600,1,0,0);__pocketCall('overlay.present',10,1,c,6)",
        'private-native-command': "__pocketCall('native.shell',1)",
    }
    for name, command in bad_commands.items():
        # Catching the JS exception must not undo the host's failure latch.
        tail = '\nconst initial=PocketApplication.start;PocketApplication.start=function(c){initial(c);try{'+command+';}catch(_){}return true;};'
        pkg = variant_package(name, original+tail)
        run_case(name, [*base,'--package',pkg,'--allow-unsigned-package'],1,'APP_OPEN')
    # Integrity is not semantic validity: invalid but correctly hashed resource
    # catalogs must fail in the actual renderer bootstrap, before first pixels.
    (variant/'application.js').write_text(original)
    catalog=(variant/'catalog.json').read_bytes()
    (variant/'catalog.json').write_bytes(b'{invalid-json')
    bad_catalog=directory/'bad-catalog.pui'
    bad_catalog.write_bytes(pui.build(manifest('control-panel'),variant))
    run_case('bad-catalog', [*base,'--package',bad_catalog,'--allow-unsigned-package'],1,'ENGINE_OPEN')
    (variant/'catalog.json').write_bytes(catalog)
    # Same source plus allocation: 1 MiB fails, 8 MiB succeeds. Do not merely
    # read a number from the manifest and label it an enforced heap limit.
    source = original+'\nglobalThis.__budgetProbe=new Uint8Array(2*1024*1024);'
    small = variant_package('heap-small', source)
    run_case('heap-small', [*base,'--package',small,'--allow-unsigned-package'],1,'APP_OPEN')
    larger = manifest('control-panel'); larger['budgets']['heap_bytes'] = pui.MAX_HEAP
    large = variant_package('heap-large', source, larger)
    r = run_case('heap-large', [*base,'--package',large,'--allow-unsigned-package'])
    if r['application_heap_limit'] != pui.MAX_HEAP: raise RuntimeError('large heap policy not applied')
    exception = variant_package('private-exception', original+'\nthrow Error("R3_SYNTHETIC_EXCEPTION_SECRET");')
    run_case('private-exception', [*base,'--package',exception,'--allow-unsigned-package'],1,'APP_OPEN')
    # App-only package update changes behavior and pixels; never recompile ELF.
    changed = variant_package('increment-two', original.replace('Math.min(99,count+1)','Math.min(99,count+2)'))
    r = run_case('increment-two', [*base,'--package',changed,'--allow-unsigned-package','--replay-input',replay])
    if r['application_selection'] != 2 or r['completed'] != 1 or f.sha(directory/'increment-two/last.ppm') == results['control-panel-600']['last']:
        raise RuntimeError('packaged application-only change not proven')
    # Opening Coffee's real keyboard without its capability must fail before
    # any editor is created; with it, the existing keyboard suite must pass.
    no_keyboard = manifest('coffee'); no_keyboard['capabilities'].remove('ui.keyboard.ascii')
    denied = directory/'coffee-no-keyboard.pui'; denied.write_bytes(pui.build(no_keyboard, OUT/'assets'))
    keyboard_trace = directory/'keyboard.trace'; keyboard_trace.write_text('100 1 0 675 564\n140 3 0 675 564\n200 0 0 0 0\n')
    r = run_case('real-keyboard-denied', [*base,'--package',denied,'--allow-unsigned-package','--replay-input',keyboard_trace],1,'REPLAY_REJECTED')
    if r['text_input_opens']: raise RuntimeError('denied keyboard was created')
    r = run_case('real-keyboard-open', [*base,'--package',packages['coffee'],'--allow-unsigned-package','--replay-input',keyboard_trace])
    if not r['text_input_open'] or (directory/'real-keyboard-open/last.ppm').exists(): raise RuntimeError('open editor snapshot leaked')
    # Nondestructive failure: an existing output directory cannot be overwritten.
    prior = f.sha(directory/'coffee-600/report.json')
    f.run([*prefix,*base,'--package',packages['coffee'],'--allow-unsigned-package','--output',directory/'coffee-600'],directory/'overwrite.log',2)
    if f.sha(directory/'coffee-600/report.json') != prior: raise RuntimeError('previous report overwritten')
    if before != f.state() or f.sha(binary) != runtime_sha: raise RuntimeError('source/ELF changed during package proof')
    return {**before, 'mode':mode, 'static':static, 'scope':'production-elf-pui-development',
        'physical_hardware':False, 'authenticated':False, 'production_admission':False,
        'runtime_sha256':runtime_sha, 'ownership_sha256':f.sha(binary.with_suffix(binary.suffix+'.ownership.json')),
        'cases':count_cases, 'same_binary_packaged_applications':True, 'capability_enforcement':True, 'heap_enforcement':True,
        'runs':results, 'directory':directory.relative_to(OUT).as_posix(), 'evidence':files(directory)}


def test_runtime_packages(mode: str, sanitize: bool = False, current_cases: Path | None = None) -> None:
    before = f.state(); directory = f.fresh(OUT/mode, 'package-sanitize' if sanitize else 'package-runtime')
    packages = build_packages(directory/'packages')
    # Existing unchanged test bodies exercise media, all Coffee routes, scrolling
    # and keyboard privacy through a thin TEST-ONLY package-open adapter.
    shutil.copyfile(packages['coffee'], Path(str(OUT/'assets')+'.pui'))
    binary = f.compile_binary(mode, test=True, sanitize=sanitize, package_test='framework')
    images = []
    for name in ('cases','repeat','negative'):
        dest = directory/name; dest.mkdir()
        text = f.run([*f.runner(mode),binary,OUT/'assets',dest,*(['--intentional-failure'] if name=='negative' else [])],
                     directory/(name+'.log'),1 if name=='negative' else 0)
        if name=='negative':
            if 'INTENTIONAL_ASSERTION_FAILURE' not in text or 'FRAMEWORK_OK' in text: raise RuntimeError('package pixel negative failed open')
        else:
            observed = legacy_outputs(dest)
            if 'FRAMEWORK_OK' not in text or len(observed)!=48: raise RuntimeError('package workload matrix incomplete')
            images.append(observed)
    if images[0]!=images[1]: raise RuntimeError('package workload nondeterministic')
    if current_cases is not None and images[0]!=legacy_outputs(current_cases): raise RuntimeError('package migration changed old pixels/replays')
    life = f.compile_binary(mode,test=True,sanitize=sanitize,package_test='lifetime')
    life_images=[]
    for name in ('lifetime','lifetime-repeat','lifetime-negative'):
        dest=directory/name;dest.mkdir()
        neg=name.endswith('negative')
        text=f.run([*f.runner(mode),life,packages['control-panel'],dest,*(['--negative'] if neg else [])],directory/(name+'.log'),1 if neg else 0)
        if neg:
            if 'INTENTIONAL_PACKAGE_LIFETIME_ASSERTION_FAILURE' not in text:raise RuntimeError('lifetime negative missing')
        else:
            if 'PACKAGE_LIFETIME_OK' not in text:raise RuntimeError('lifetime checks missing')
            life_images.append(files(dest,lambda p:p.suffix=='.ppm'))
    if life_images[0]!=life_images[1] or len(life_images[0])!=2:raise RuntimeError('retained package pixel drift')
    proof=None if sanitize else prove_package_binary(OUT/mode/'ui-framework',mode,directory/'production-proof')
    if before!=f.state():raise RuntimeError('package sources changed during test')
    f.write(OUT/mode/('package-runtime-sanitize.json' if sanitize else 'package-runtime.json'),{
        **before,'mode':mode,'sanitizer':sanitize,'physical_hardware':False,'authenticated':False,
        'binary':binary.relative_to(OUT).as_posix(),'binary_sha256':f.sha(binary),
        'lifetime_binary':life.relative_to(OUT).as_posix(),'lifetime_binary_sha256':f.sha(life),
        'baseline_outputs':images[0],'lifetime_images':life_images[0], 'production_proof':proof,
        'directory':directory.relative_to(OUT).as_posix(),'evidence':files(directory)})
    print('PACKAGE_RUNTIME_OK',mode,'sanitizer='+str(sanitize),'physical=false authenticated=false')


def verify_runtime_packages(current: dict) -> None:
    reports=[]
    for mode,sanitize in (('native',False),('arm',False),('native',True)):
        path=OUT/mode/('package-runtime-sanitize.json' if sanitize else 'package-runtime.json')
        r=json.loads(path.read_text())
        if any(r[k]!=v for k,v in current.items()) or r['mode']!=mode or r['sanitizer'] is not sanitize or r['physical_hardware'] or r['authenticated']:
            raise RuntimeError('stale/misleading package integration report')
        if f.sha(OUT/r['binary'])!=r['binary_sha256'] or f.sha(OUT/r['lifetime_binary'])!=r['lifetime_binary_sha256']:
            raise RuntimeError('package test binary changed')
        if files(OUT/r['directory'])!=r['evidence']:raise RuntimeError('package evidence changed')
        if len(r['baseline_outputs'])!=48 or len(r['lifetime_images'])!=2:raise RuntimeError('incomplete package image matrix')
        if not sanitize:
            proof=r['production_proof'];binary=OUT/mode/'ui-framework'
            if not proof or proof['runtime_sha256']!=f.sha(binary) or not proof['same_binary_packaged_applications'] or proof['cases']<28:
                raise RuntimeError('actual packaged production execution missing')
            if files(OUT/proof['directory'])!=proof['evidence'] or proof['authenticated'] or proof['physical_hardware'] or proof['production_admission']:
                raise RuntimeError('package proof evidence/scope mismatch')
        reports.append(r)
    for r in reports[1:]:
        if r['baseline_outputs']!=reports[0]['baseline_outputs'] or r['lifetime_images']!=reports[0]['lifetime_images']:
            raise RuntimeError('package cross-architecture/sanitizer output mismatch')
    if reports[0]['production_proof']['runs'] != {k:{**v,'runtime_sha256':reports[0]['production_proof']['runtime_sha256']} for k,v in reports[1]['production_proof']['runs'].items()}:
        raise RuntimeError('Native/ARM packaged application behavior mismatch')
    f.write(OUT/'package-runtime-verification.json',{**current,'status':'passed',
        'scope':'software-pui-development-loader','physical_hardware':False,'authenticated':False,'production_admission':False,
        'same_binary_packaged_applications':True,'immutable_payload_lifetime':True,'capability_enforcement':True,'heap_enforcement':True,
        'baseline_images':42,'baseline_replays':6,'reports':{mode+('-sanitize' if san else ''):
            f.sha(OUT/mode/('package-runtime-sanitize.json' if san else 'package-runtime.json')) for mode,san in (('native',False),('arm',False),('native',True))}})
    print('PACKAGE_RUNTIME_VERIFIED development-only no-authentication no-hardware')
