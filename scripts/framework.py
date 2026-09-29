#!/usr/bin/env python3
"""Build/test the real framework. Only independently returned board evidence is HIL."""
from __future__ import annotations
import argparse, hashlib, importlib.util, json, os, shutil, subprocess, sys, tarfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/framework'
UI=['object','layout','style','component','navigation','overlay','reactive','model','interaction','scene']
PROFILES=['imx6ul-1024x600','imx6ul-1024x800']

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,obj):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(obj,indent=2,sort_keys=True)+'\n')
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
def run(args,log,expected=0,timeout=900):
    args=list(map(str,args));print('+',' '.join(args),flush=True)
    r=subprocess.run(args,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
    log.parent.mkdir(parents=True,exist_ok=True);log.write_text('$ '+' '.join(args)+'\n'+r.stdout+f'\nexit={r.returncode}\n');print(r.stdout,end='',flush=True)
    if r.returncode!=expected:raise RuntimeError(f'exit {r.returncode}, expected {expected}: {log}')
    return r.stdout

def state():
    development=os.environ.get('FRAMEWORK_DEVELOPMENT')=='1'
    if not development and git('status','--porcelain','--untracked-files=all'):raise RuntimeError('committed clean checkout required')
    files=git('ls-files').splitlines()
    return {'commit':git('rev-parse','HEAD'),'tree':git('rev-parse','HEAD^{tree}'),
            'source_files':{n:sha(ROOT/n) for n in files if (ROOT/n).is_file()},'development':development}

def assets():
    src=ROOT/'out/coffee/assets';d=OUT/'assets';d.mkdir(parents=True,exist_ok=True)
    report=json.loads((ROOT/'out/coffee/assets.json').read_text())
    if report['font_source']['debug_only']:raise RuntimeError('debug font rejected')
    for name in ('labels.atlas','builtin.rgba','alternate.rgba','Noto-LICENSE.txt','IMAGE-LICENSE.txt'):
        shutil.copy2(src/name,d/name)
    locales=json.loads((ROOT/'apps/coffee-demo/locales.json').read_text())
    if list(locales)!=['zh-CN','en-US','de-DE','fr-FR','es-ES','pt-PT']:raise RuntimeError('locale ordering changed')
    keys=['title','subtitle','demo','next','back','cancel','start','confirm','making','done','home','media'];catalogs=[]
    for locale,labels in locales.items():
        t={i+1:labels[k] for i,k in enumerate(keys)};t.update({13:locale,14:'Theme'})
        t.update({100+i:name for i,name in enumerate(labels['names'])});catalogs.append(t)
    (d/'framework.js').write_text('const POCKET_TEXT_CATALOG='+json.dumps(catalogs,ensure_ascii=True,separators=(',',':'))+';\n'+(ROOT/'hosts/linux/engine/scene_guest.js').read_text())
    write(OUT/'assets.json',{'p4_assets_manifest':sha(ROOT/'out/coffee/assets.json'),
         'files':{p.name:sha(p) for p in sorted(d.iterdir()) if p.is_file()}})

def runtime_check(mode):
    if os.environ.get('FRAMEWORK_DEVELOPMENT')=='1':return
    spec=importlib.util.spec_from_file_location('framework_runtime_build',ROOT/'scripts/runtime.py')
    rt=importlib.util.module_from_spec(spec);spec.loader.exec_module(rt)
    data,_,_=rt.config();rt.check_build(data,mode)

def compile_binary(mode,test=False,sanitize=False,static=False,loop=False):
    runtime_check(mode);state()
    d=OUT/mode;d.mkdir(parents=True,exist_ok=True)
    cc='gcc' if mode=='native' else 'arm-linux-gnueabihf-gcc'
    flags=[] if mode=='native' else ['-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard']
    target='x86_64-unknown-linux-gnu' if mode=='native' else 'armv7-unknown-linux-gnueabihf'
    rt=ROOT/'out/runtime'/mode;core=ROOT/'out/runtime/cargo'/target/'release/libpocketjs_symbian_core.a'
    sources=[ROOT/'hosts/linux/ui'/f'{n}.c' for n in UI]+[ROOT/'apps/coffee-framework/app.c',ROOT/'hosts/linux/engine/scene_runtime.c',ROOT/'hosts/linux/input/interaction_bridge.c',ROOT/'hosts/linux/framework.c']
    sources+=[ROOT/('tests/framework/test_live_loop.c' if loop else 'tests/framework/test_framework.c' if test else 'hosts/linux/framework_main.c')]
    objects=[rt/n for n in ('host.o','platform.o','runtime-host.o','personality.o','libquickjs.a','media-store.o')]
    if not test:sources += [ROOT/'hosts/linux/input/live.c',ROOT/'hosts/linux/input/state.c',ROOT/'hosts/linux/display/fbdev.c',ROOT/'hosts/linux/display/presenter.c']
    binary=d/('framework-loop' if loop else 'framework-test' if test else 'ui-framework')
    if sanitize:binary=binary.with_name(binary.name+'-sanitize')
    if static:binary=binary.with_name(binary.name+'-static')
    opts=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g'] if sanitize else []
    cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O2',*flags,*opts,
       '-I.','-Ihosts/linux','-DPOCKET_BUILD_COMMIT="'+git('rev-parse','HEAD')+'"',
       '-Iout/runtime/include','-Iout/runtime/source-'+mode+'/engine/quickjs-c',*sources,*objects,core,
       *(['-static'] if static else []),'-Wl,--gc-sections','-lm','-ldl','-lpthread','-lrt','-o',binary]
    if loop:
        wrappers=['host_monotonic_ns','host_sleep_until','fbdev_open','fbdev_close','fbdev_report','fbdev_present','ioctl','input_live_discover','input_live_close','input_live_wait','input_live_reconnect','input_live_drain']
        cmd += ['-Wl,--wrap='+name for name in wrappers]
    run(cmd,d/(binary.name+'-build.log'));return binary

def runner(mode,static=False):return [] if mode=='native' else ['qemu-arm','-cpu','cortex-a7',*([] if static else ['-L','/usr/arm-linux-gnueabihf'])]
def outputs(d,pattern):return {p.name:sha(p) for p in sorted(d.glob(pattern))}
def fresh(parent,name):
    p=parent/(name+'-'+str(time.time_ns()));p.mkdir(parents=True);return p

def cli_checks(binary,mode,dir,static=False):
    dir.mkdir(parents=True,exist_ok=True)
    base=[*runner(mode,static),binary];assets_dir=OUT/'assets'
    for profile in PROFILES:
        dest=dir/profile
        run([*base,'--headless','--profile',profile,'--asset-root',assets_dir,'--seconds','1','--output',dest],dir/(profile+'.log'))
        r=json.loads((dest/'report.json').read_text())
        if not r['ok'] or r['physical_io'] or r['visual_validated'] or r['core_live_bytes_after_close'] or r['presents']!=2 or r['clean_skips']<1:raise RuntimeError('production headless CLI gate failed')
        if r['commit']!=git('rev-parse','HEAD') or sha(dest/'first.ppm')!=sha(dest/'last.ppm'):raise RuntimeError('CLI source/idle image mismatch')
        before=sha(dest/'report.json')
        run([*base,'--headless','--profile',profile,'--asset-root',assets_dir,'--seconds','1','--output',dest],dir/(profile+'-overwrite.log'),2)
        if sha(dest/'report.json')!=before:raise RuntimeError('old report was overwritten')
    common=['--profile',PROFILES[0],'--asset-root',assets_dir,'--seconds','1']
    run([*base,*common,'--output',dir/'no-consent'],dir/'no-consent.log',2)
    if (dir/'no-consent').exists():raise RuntimeError('write admitted without token')
    run([*base,*common,'--allow-write','I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER','--fbdev','/dev/null','--output',dir/'not-fb'],dir/'not-fb.log',1)
    r=json.loads((dir/'not-fb/report.json').read_text())
    if r['ok'] or r['physical_io'] or r['presents']:raise RuntimeError('invalid framebuffer admitted')
    run([*base,'--profile',PROFILES[1],'--asset-root',assets_dir,'--output',dir/'unknown-board','--allow-write','I_UNDERSTAND_THIS_WRITES_FRAMEBUFFER'],dir/'unknown-board.log',2)

def test(mode,sanitize=False):
    assets();before=state();binary=compile_binary(mode,True,sanitize);d=fresh(OUT/mode,'sanitizer' if sanitize else 'test')
    sets=[]
    for label in ('cases','repeat','negative'):
        dest=d/label;dest.mkdir();sets.append(dest)
        text=run([*runner(mode),binary,OUT/'assets',dest,*(['--intentional-failure'] if label=='negative' else [])],d/(label+'.log'),1 if label=='negative' else 0)
        if label=='negative':
            if 'INTENTIONAL_ASSERTION_FAILURE' not in text or 'FRAMEWORK_OK' in text:raise RuntimeError('negative gate failed open')
        elif 'FRAMEWORK_OK dual-viewport real-core' not in text:raise RuntimeError('missing real suite completion')
    for pattern in ('*.ppm','replay-*.txt'):
        if outputs(sets[0],pattern)!=outputs(sets[1],pattern):raise RuntimeError('nondeterministic native/ARM replay')
    images=outputs(sets[0],'*.ppm');replays=outputs(sets[0],'replay-*.txt')
    if len(images)!=18 or len(replays)!=4:raise RuntimeError('incomplete target matrix')
    if not sanitize:
        cli=compile_binary(mode);cli_checks(cli,mode,d/'cli')
        loop=compile_binary(mode,loop=True)
        for height in (600,800):
            dest=d/('loop-'+str(height))
            text=run([*runner(mode),loop,OUT/'assets',dest,str(height)],d/('loop-'+str(height)+'.log'))
            r=json.loads((dest/'report.json').read_text())
            if 'FRAMEWORK_LOOP_OK' not in text or r.get('synthetic') is not True or r['physical_io'] or not r['ok'] or r['page_mask']!=15 or r['completed']!=1 or r['disconnects']!=1 or r['reconnects']!=1 or r['syn_dropped']!=1:raise RuntimeError('production device loop gate failed')
    if before!=state():raise RuntimeError('source changed during test')
    record={**before,'mode':mode,'real_core':True,'physical_hardware':False,'sanitizer':sanitize,
       'run_dir':str(d.relative_to(OUT)),'test_binary_sha256':sha(binary),'assets_sha256':sha(OUT/'assets.json'),
       'images':images,'replays':replays,'logs':{str(p.relative_to(d)):sha(p) for p in sorted(d.rglob('*.log'))}}
    write(OUT/mode/('sanitizer.json' if sanitize else 'test.json'),record);print('FRAMEWORK_TEST_OK',mode,'sanitizer='+str(sanitize))

def verify():
    current=state()
    if current['development']:raise RuntimeError('development-only evidence is not acceptance')
    results=[]
    for mode in ('native','arm'):
        r=json.loads((OUT/mode/'test.json').read_text());runtime_check(mode)
        if any(r[k]!=current[k] for k in current) or r['physical_hardware'] or not r['real_core']:raise RuntimeError('stale or invalid source evidence')
        if r['assets_sha256']!=sha(OUT/'assets.json') or r['test_binary_sha256']!=sha(OUT/mode/'framework-test'):raise RuntimeError('binary/resource drift')
        d=OUT/r['run_dir']
        if r['images']!=outputs(d/'cases','*.ppm') or r['replays']!=outputs(d/'cases','replay-*.txt'):raise RuntimeError('output evidence drift')
        for n,v in r['logs'].items():
            if sha(d/n)!=v:raise RuntimeError('log evidence drift')
        results.append(r)
    if results[0]['images']!=results[1]['images'] or results[0]['replays']!=results[1]['replays']:raise RuntimeError('cross-architecture pixel/replay mismatch')
    sanitized=json.loads((OUT/'native/sanitizer.json').read_text())
    if any(sanitized[k]!=current[k] for k in current) or not sanitized.get('sanitizer') or sanitized['images']!=results[0]['images'] or sanitized['replays']!=results[0]['replays']:raise RuntimeError('sanitizer evidence incomplete or stale')
    if sanitized['test_binary_sha256']!=sha(OUT/'native/framework-test-sanitize') or sanitized['assets_sha256']!=sha(OUT/'assets.json'):raise RuntimeError('sanitizer binary/resource drift')
    for n,v in sanitized['logs'].items():
        if sha(OUT/sanitized['run_dir']/n)!=v:raise RuntimeError('sanitizer log evidence drift')
    write(OUT/'verification.json',{'status':'passed',**current,'scope':'software-functional-only','physical_hardware':False,
          'engine':'current-pocket-scene','image_count':18,'replay_count':4,'test_reports':{m:sha(OUT/m/'test.json') for m in ('native','arm')}})
    print('FRAMEWORK_VERIFY_OK physical_hardware=false')

def sums(d):
    files=[p for p in sorted(d.rglob('*')) if p.is_file() and p.name!='SHA256SUMS']
    (d/'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.relative_to(d)}\n' for p in files))

def package():
    verify();binary=compile_binary('arm',static=True);d=fresh(OUT,'package-check')
    elf=run(['arm-linux-gnueabihf-readelf','-h','-A','-l','-d',binary],d/'elf.log')
    if 'ELF32' not in elf or 'Machine:                           ARM' not in elf or 'Tag_ABI_VFP_args: VFP registers' not in elf or 'INTERP' in elf or 'NEEDED' in elf:raise RuntimeError('ARM static ABI gate')
    symbols=run(['arm-linux-gnueabihf-nm',binary],d/'symbols.log')
    if any(x in symbols for x in ('__wrap_','pocket_runtime_harness_','fake_fb')):raise RuntimeError('test symbol leak')
    for name in ('pocket_framework_tick','pocket_interaction_pointer','pocket_reactive_flush','input_live_discover','fbdev_present','JS_Eval'):
        if name not in symbols:raise RuntimeError('missing real chain: '+name)
    cli_checks(binary,'arm',d/'cli',static=True)
    dest=OUT/'device/coffee-framework'
    if dest.exists():shutil.rmtree(dest)
    dest.mkdir(parents=True);shutil.copy2(binary,dest/'ui-framework');(dest/'ui-framework').chmod(0o755)
    shutil.copytree(OUT/'assets',dest/'assets');(dest/'assets/alternate.rgba').unlink()
    shutil.copy2(ROOT/'docs/framework-board.md',dest/'README.md')
    shutil.copy2(ROOT/'scripts/device/run-framework.sh',dest/'run-framework.sh');(dest/'run-framework.sh').chmod(0o755)
    # Reuse the already signed/validated P4 installer and its public demo packages.
    old=ROOT/'out/coffee/device/coffee-demo'
    shutil.copy2(old/'mediactl',dest/'mediactl');(dest/'mediactl').chmod(0o755)
    shutil.copytree(old/'updates',dest/'updates')
    shutil.copy2(OUT/'verification.json',dest/'software-verification.json')
    write(dest/'manifest.json',{'schema':1,'source_commit':git('rev-parse','HEAD'),'source_tree':git('rev-parse','HEAD^{tree}'),
       'profiles':PROFILES,'binary':'ui-framework','binary_sha256':sha(dest/'ui-framework'),'static':True,
       'engine':'current-pocket-scene','p4_package_manifest':sha(old/'manifest.json'),
       'software_verification_sha256':sha(dest/'software-verification.json'),'physical_hardware':False,
       'physical_gate':'pending independent 600 and 800 reports and human review',
       'business_commands':False,'input_method':False,'source_font_included':False})
    for p in dest.rglob('*'):
        if p.suffix.lower() in ('.ttf','.otf','.ttc','.key','.pem') or 'private' in p.name.lower():raise RuntimeError('private/font asset in package')
    sums(dest);archive=OUT/'imx6ul-coffee-framework.tar.gz'
    with tarfile.open(archive,'w:gz') as t:t.add(dest,arcname='coffee-framework')
    write(OUT/'package.json',{'commit':git('rev-parse','HEAD'),'archive':archive.name,'sha256':sha(archive),'abi_log':str((d/'elf.log').relative_to(OUT)),
        'static_binary_sha256':sha(binary),'verification_sha256':sha(OUT/'verification.json'),'physical_hardware':False})
    print('FRAMEWORK_PACKAGE_OK',sha(archive))

def package_check():
    verify();m=json.loads((OUT/'package.json').read_text());dest=OUT/'device/coffee-framework'
    if m['commit']!=git('rev-parse','HEAD') or sha(OUT/m['archive'])!=m['sha256'] or m['verification_sha256']!=sha(OUT/'verification.json'):raise RuntimeError('stale package')
    for line in (dest/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split('  ',1)
        if Path(name).is_absolute() or '..' in Path(name).parts or sha(dest/name)!=digest:raise RuntimeError('package member mismatch')
    if sha(dest/'ui-framework')!=m['static_binary_sha256']:raise RuntimeError('deployed ELF differs')
    print('FRAMEWORK_PACKAGE_VERIFIED physical_hardware=false')

def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['assets','test','build','verify','package','package-check']);p.add_argument('--mode',choices=['native','arm'],default='native');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    if a.action=='assets':assets()
    elif a.action=='test':test(a.mode,a.sanitize)
    elif a.action=='build':assets();compile_binary(a.mode)
    elif a.action=='verify':verify()
    elif a.action=='package':package()
    elif a.action=='package-check':package_check()
if __name__=='__main__':
    try:main()
    except Exception as e:print('FRAMEWORK_FAILED:',e,file=sys.stderr);sys.exit(1)
