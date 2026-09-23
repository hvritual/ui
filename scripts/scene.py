#!/usr/bin/env python3
"""Dynamic images/carousel: signed data, real Core tests, bounded board package."""
from __future__ import annotations
import copy, hashlib, importlib.util, json, os, shutil, subprocess, sys, tarfile, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'out/scene'
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
rt=load('runtime',ROOT/'scripts/runtime.py')
def run(args,log,expected=0,cwd=ROOT,env=None,timeout=240):
 log.parent.mkdir(parents=True,exist_ok=True)
 p=subprocess.run(list(map(str,args)),cwd=cwd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
 log.write_text('$ '+' '.join(map(str,args))+'\n'+p.stdout+f'\nexit={p.returncode}\n');print(p.stdout,end='')
 if p.returncode!=expected:raise RuntimeError(f'command failed {log}')
 return p.stdout

def build():
 OUT.mkdir(parents=True,exist_ok=True);assets=OUT/'assets';assets.mkdir(exist_ok=True)
 for name in ('labels.atlas','Noto-LICENSE.txt','IMAGE-LICENSE.txt'):shutil.copy2(ROOT/'out/coffee/assets'/name,assets/name)
 (assets/'media-scene.js').write_text((ROOT/'apps/media-scene/runtime.js').read_text()+'\n'+(ROOT/'apps/media-scene/app.js').read_text())
 media=OUT/'mediactl';run(['go','build','-trimpath','-o',media,'.'],OUT/'media-build.log',cwd=ROOT/'tools/media',env={**os.environ,'CGO_ENABLED':'0'})
 examples=OUT/'examples';examples.mkdir(exist_ok=True);(examples/'images').mkdir(exist_ok=True)
 run([media,'scene-fixture',examples],OUT/'scene-fixture.log')
 names=['espresso','americano','latte','cappuccino','flatwhite','mocha','tea','water','latte']
 registry=[]
 for i,name in enumerate(names):
  identifier=f'campaign_{i}'
  # Fixtures are baked for their declared texture dimensions, not upscaled from Coffee cards.
  registry.append({'id':identifier,'file':f'images/{identifier}.png','sha256':'','license':'Original diagnostic fixture; operator replacement requires permission','width':512 if i in (0,2,6) else 128,'height':256 if i in (0,2,6) else 64})
 item=lambda i,ms:{'asset':f'campaign_{i}','hold_ms':ms}
 def element(id,kind,x,y,w,h,z,items,loop=False,visible=True):return dict(id=id,kind=kind,x=x,y=y,width=w,height=h,z=z,visible=visible,fit='contain',loop=loop,items=items)
 a=dict(schema=1,app='media-scene',version='scene-a',assets=registry,elements=[element('hero','carousel',48,24,512,360,0,[item(0,2000),item(2,3000),item(6,1500)],True),element('badge','image',800,176,128,64,10,[item(8,1000)])])
 b=copy.deepcopy(a);b['version']='scene-b';b['elements']=[element('hero','carousel',160,48,512,320,0,[item(6,2000),item(2,3000),item(0,1500)],True),element('new_logo','image',784,64,128,64,10,[item(3,1000)]),element('new_stamp','image',784,280,128,64,20,[item(7,1000)])]
 hidden=copy.deepcopy(b);hidden['version']='scene-hidden';hidden['elements'][1]['visible']=False
 empty=copy.deepcopy(a);empty['version']='scene-empty';empty['elements']=[]
 updates=OUT/'updates';updates.mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory() as temp:
  prefix=Path(temp)/'signer';run([media,'keygen',prefix],OUT/'keygen.log');shutil.copy2(str(prefix)+'.public',updates/'scene.public')
  for name,doc in [('a',a),('b',b),('hidden',hidden),('empty',empty)]:
   config=examples/f'scene-{name}.json';config.write_text(json.dumps(doc,indent=2)+'\n')
   bundle=updates/f'scene-{name}.zip';bundle.unlink(missing_ok=True)
   run([media,'scene-pack',examples,config,str(prefix)+'.private',bundle],OUT/f'pack-{name}.log')
   store=Path(temp)/name;run([media,'scene-install',store,updates/'scene.public',bundle],OUT/f'install-{name}.log')
   ident=(store/'current').read_text().splitlines()[0]
   shutil.copy2(store/(ident+'.rgba'),assets/('scene.packet' if name=='a' else f'scene-{name}.packet'))
 # Clock/runtime unit tests do not count as real renderer acceptance.
 run(['node','tests/scene/test_clock.js'],OUT/'clock.log')
 print('SCENE_ASSETS_OK registry=9 elements=2/3/hidden/empty')

def test(mode):
 data,_,_=rt.config();rt.check_build(data,mode);tool=data['targets'][mode];d=OUT/mode;d.mkdir(parents=True,exist_ok=True)
 prepared=rt.OUT/('source-'+mode);core=rt.OUT/'cargo'/tool['triple']/'release/libpocketjs_symbian_core.a'
 objs=[rt.OUT/mode/n for n in ('host.o','platform.o','media-store.o','runtime-test.o','personality.o','libquickjs.a')]
 binary=d/'scene-test';flags=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O2',*tool['c_flags'],'-Ihosts/linux','-I'+str(prepared/'engine/quickjs-c')]
 run([tool['cc'],*flags,'tests/scene/test_scene.c',*objs,core,'-Wl,--gc-sections','-lm','-ldl','-lpthread','-lrt','-o',binary],d/'build.log')
 text=run([*tool['runner'],binary,OUT/'assets',d],d/'test.log')
 if 'SCENE_OK checks=24' not in text or text.count('PASS media-scene-')!=2:raise RuntimeError('incomplete scene tests')
 text=run([*tool['runner'],binary,OUT/'assets',d,'--fail'],d/'negative.log',expected=1)
 if 'SCENE_OK' in text or 'SCENE_FAIL' not in text:raise RuntimeError('negative scene gate failed open')
 loop=d/'scene-idle-loop'
 extra=[rt.OUT/mode/n for n in ('input-state.o','input-bridge.o','input-live.o','input-cli.o','display-fbdev.o','display-presenter.o')]
 wraps=['input_live_discover','input_live_reconnect','input_live_wait','input_live_drain','input_live_close','fbdev_open','fbdev_close','fbdev_present','host_monotonic_ns','poll']
 run([tool['cc'],*flags,'tests/scene/test_loop.c',*objs,*extra,core,*['-Wl,--wrap='+w for w in wraps],'-Wl,--gc-sections','-lm','-ldl','-lpthread','-lrt','-o',loop],d/'loop-build.log')
 report=d/'idle.json';timeline=d/'idle.csv';report.unlink(missing_ok=True);timeline.unlink(missing_ok=True)
 text=run([*tool['runner'],loop,OUT/'assets',report,timeline],d/'loop.log')
 r=rt.read_json(report)
 if 'SCENE_IDLE_LOOP_OK' not in text or r['operation']!='media-scene' or r['host']['turns']!=61 or r['display']['presents']!=2 or r['presentation']['clean_frames_skipped']!=30:raise RuntimeError('scene production CLI idle regression')
 rt.write_json(d/'test.json',{**rt.project_state(),'mode':mode,'build':rt.digest(rt.OUT/mode/'build.json'),'binary':rt.digest(binary),'idle_report':rt.digest(report),'loop_binary':rt.digest(loop),'assets':{p.name:rt.digest(p) for p in sorted((OUT/'assets').iterdir()) if p.is_file()},'goldens':{p.name:rt.digest(p) for p in sorted(d.glob('*.ppm'))},'physical_hardware':False})

def verify():
 state=rt.project_state();results={}
 clock=(OUT/'clock.log').read_text()
 if 'SCENE_CLOCK_OK cases=4' not in clock or clock.count('PASS scene-')!=4:raise RuntimeError('clock evidence incomplete')
 for mode in ('native','arm'):
  r=rt.read_json(OUT/mode/'test.json');results[mode]=r
  if r['commit']!=state['commit'] or r['source_files']!=state['source_files'] or r['build']!=rt.digest(rt.OUT/mode/'build.json') or r['binary']!=rt.digest(OUT/mode/'scene-test'):raise RuntimeError('stale scene evidence')
  if r['idle_report']!=rt.digest(OUT/mode/'idle.json') or r['loop_binary']!=rt.digest(OUT/mode/'scene-idle-loop'):raise RuntimeError('scene idle evidence changed')
  for name,digest in r['assets'].items():
   if digest!=rt.digest(OUT/'assets'/name):raise RuntimeError('scene asset changed')
  for name,digest in r['goldens'].items():
   if digest!=rt.digest(OUT/mode/name):raise RuntimeError('scene pixels changed')
 if results['native']['goldens']!=results['arm']['goldens'] or len(results['arm']['goldens'])!=10:raise RuntimeError('cross-architecture scene mismatch')
 rt.write_json(OUT/'verification.json',{'commit':state['commit'],'status':'passed','goldens':10,'clock_log_sha256':rt.digest(OUT/'clock.log'),'real_core_checks':24,'physical_hardware':False,'video':False,'tests':{m:rt.digest(OUT/m/'test.json') for m in results}})
 print('SCENE_VERIFY_OK')

def package():
 v=rt.read_json(OUT/'verification.json')
 if v['commit']!=rt.project_state()['commit']:raise RuntimeError('stale scene package')
 dest=OUT/'device/media-scene';dest.mkdir(parents=True,exist_ok=True);(dest/'assets').mkdir(exist_ok=True)
 shutil.copy2(ROOT/'out/device/imx6ul/ui-host-imx6ul-static',dest/'ui-host-imx6ul-static')
 for name in ('media-scene.js','labels.atlas','scene.packet','Noto-LICENSE.txt','IMAGE-LICENSE.txt'):shutil.copy2(OUT/'assets'/name,dest/'assets'/name)
 for name in ('updates','examples'):shutil.copytree(OUT/name,dest/name,dirs_exist_ok=True)
 shutil.copy2(ROOT/'scripts/device/run-media-scene.sh',dest/'run-media-scene.sh');shutil.copy2(ROOT/'docs/media-scene.md',dest/'README.md')
 run(['go','build','-trimpath','-ldflags=-s -w','-o',dest/'mediactl','.'],OUT/'device-build.log',cwd=ROOT/'tools/media',env={**os.environ,'CGO_ENABLED':'0','GOOS':'linux','GOARCH':'arm','GOARM':'7'})
 for name in ('mediactl','ui-host-imx6ul-static','run-media-scene.sh'):(dest/name).chmod(0o755)
 for name in ('mediactl','ui-host-imx6ul-static'):
  report=run(['arm-linux-gnueabihf-readelf','-h','-l','-d',dest/name],OUT/f'{name}-elf.log')
  if 'INTERP' in report or 'NEEDED' in report or 'ELF32' not in report or 'ARM' not in report:raise RuntimeError('device ABI')
 store=OUT/'arm-package-store'
 for name in ('a','b'):run(['qemu-arm','-cpu','cortex-a7',dest/'mediactl','scene-install',store,dest/'updates/scene.public',dest/f'updates/scene-{name}.zip'],OUT/f'arm-package-{name}.log')
 run(['qemu-arm','-cpu','cortex-a7',dest/'mediactl','scene-rollback',store,dest/'updates/scene.public'],OUT/'arm-package-rollback.log')
 rt.write_json(dest/'manifest.json',{'source_commit':rt.project_state()['commit'],'scene_verification':rt.digest(OUT/'verification.json'),'ui_sha256':rt.digest(dest/'ui-host-imx6ul-static'),'physical_scene_tested':False,'video':False})
 files=[p for p in dest.rglob('*') if p.is_file() and p.name!='SHA256SUMS']
 (dest/'SHA256SUMS').write_text(''.join(f'{rt.digest(p)}  {p.relative_to(dest)}\n' for p in sorted(files)))
 with tarfile.open(OUT/'imx6ul-media-scene.tar.gz','w:gz') as t:t.add(dest,arcname='media-scene')
 print('SCENE_PACKAGE_OK sha256='+rt.digest(OUT/'imx6ul-media-scene.tar.gz'))

if __name__=='__main__':
 try:
  action=sys.argv[1]
  if action=='build':build()
  elif action=='test':test(sys.argv[2])
  elif action=='verify':verify()
  elif action=='package':package()
  else:raise RuntimeError('unknown scene command')
 except Exception as e:print('SCENE_FAILED: '+str(e),file=sys.stderr);sys.exit(1)
