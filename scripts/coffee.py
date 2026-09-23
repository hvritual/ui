#!/usr/bin/env python3
"""P4 real PocketJS tests and device package. No QEMU performance claims."""
from __future__ import annotations
import hashlib, importlib.util, json, os, shutil, subprocess, sys, tarfile, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; OUT=ROOT/'out/coffee'
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
rt=load('runtime',ROOT/'scripts/runtime.py')
def run(args,log,expected=0,env=None,cwd=ROOT,timeout=240):
 log.parent.mkdir(parents=True,exist_ok=True)
 r=subprocess.run(list(map(str,args)),cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=timeout)
 log.write_text('$ '+' '.join(map(str,args))+'\n'+r.stdout+f'\nexit={r.returncode}\n');print(r.stdout,end='')
 if r.returncode!=expected:raise RuntimeError(f'command failed: {log}')
 return r.stdout

def test(mode):
 data,_,_=rt.config();build=rt.check_build(data,mode);tool=data['targets'][mode];d=OUT/mode;d.mkdir(parents=True,exist_ok=True)
 assets=json.loads((OUT/'assets.json').read_text())
 if assets['font_source']['debug_only']:raise RuntimeError('debug font cannot be accepted')
 prepared=rt.OUT/('source-'+mode);core=rt.OUT/'cargo'/tool['triple']/'release/libpocketjs_symbian_core.a'
 binary=d/'coffee-test'
 objs=[rt.OUT/mode/n for n in ('host.o','platform.o','media-store.o','runtime-test.o','personality.o','libquickjs.a')]
 run([tool['cc'],'-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O2',*tool['c_flags'],'-Ihosts/linux','-I'+str(prepared/'engine/quickjs-c'),'-I'+str(prepared/'engine/ui-cabi/include'),'tests/coffee/test_demo.c',*objs,core,'-Wl,--gc-sections','-lm','-ldl','-lpthread','-lrt','-o',binary],d/'build.log')
 text=run([*tool['runner'],binary,OUT/'assets',d],d/'test.log')
 if 'COFFEE_OK checks=20' not in text or text.count('PASS coffee-')!=2:raise RuntimeError('incomplete scenario suite')
 neg=d/'negative';neg.mkdir(exist_ok=True)
 text=run([*tool['runner'],binary,OUT/'assets',neg,'--intentional-failure'],d/'negative.log',expected=1)
 if 'COFFEE_OK' in text or 'COFFEE_FAIL' not in text:raise RuntimeError('negative test failed open')
 rt.write_json(d/'test.json',{**rt.project_state(),'mode':mode,'runtime_build_sha256':rt.digest(rt.OUT/mode/'build.json'),'assets_sha256':rt.digest(OUT/'assets.json'),'binary_sha256':rt.digest(binary),'goldens':{p.name:rt.digest(p) for p in sorted(d.glob('*.ppm'))},'physical_coffee_tested':False,'scene':'real-pocketjs'})

def media_test():
 d=OUT/'media-tests';d.mkdir(parents=True,exist_ok=True)
 run(['go','test','-race','-count=1','-v','./...'],d/'go-native.log',cwd=ROOT/'tools/media')
 run(['go','test','-c','-o',d/'media-arm-test','.'],d/'go-arm-build.log',cwd=ROOT/'tools/media',env={**os.environ,'CGO_ENABLED':'0','GOOS':'linux','GOARCH':'arm','GOARM':'7'})
 run(['qemu-arm','-cpu','cortex-a7',d/'media-arm-test','-test.v','-test.run','TestValidate|TestInstall|TestRollback'],d/'go-arm.log',timeout=300)
 rt.write_json(d/'test.json',{**rt.project_state(),'logs':{p.name:rt.digest(p) for p in sorted(d.glob('*.log'))},'arm_binary_sha256':rt.digest(d/'media-arm-test')})

def verify():
 state=rt.project_state(); native=rt.read_json(OUT/'native/test.json');arm=rt.read_json(OUT/'arm/test.json')
 media=rt.read_json(OUT/'media-tests/test.json')
 if media['commit']!=state['commit'] or media['source_files']!=state['source_files']:raise RuntimeError('stale media evidence')
 if set(media['logs'])!={'go-native.log','go-arm-build.log','go-arm.log'}:raise RuntimeError('missing media test evidence')
 for name,digest in media['logs'].items():
  if rt.digest(OUT/'media-tests'/name)!=digest:raise RuntimeError('media log drift')
 if media['arm_binary_sha256']!=rt.digest(OUT/'media-tests/media-arm-test'):raise RuntimeError('media test binary drift')
 for mode,result in [('native',native),('arm',arm)]:
  if result['commit']!=state['commit'] or result['source_files']!=state['source_files']:raise RuntimeError('stale source evidence')
  if result['runtime_build_sha256']!=rt.digest(rt.OUT/mode/'build.json') or result['assets_sha256']!=rt.digest(OUT/'assets.json'):raise RuntimeError('stale runtime or assets')
  if result['binary_sha256']!=rt.digest(OUT/mode/'coffee-test'):raise RuntimeError('test binary changed')
  for name,digest in result['goldens'].items():
   if rt.digest(OUT/mode/name)!=digest:raise RuntimeError('changed golden')
 if native['goldens']!=arm['goldens'] or len(native['goldens'])!=22:raise RuntimeError('native/ARM full-frame golden mismatch')
 rt.write_json(OUT/'verification.json',{'status':'passed','commit':state['commit'],'scope':'P4 Coffee Demo and signed dynamic image updates','golden_count':len(native['goldens']),'physical_hardware':False,'tests':{m:rt.digest(OUT/m/'test.json') for m in ('native','arm')},'media_tests':{p.name:rt.digest(p) for p in sorted((OUT/'media-tests').glob('*.log'))}})
 print('COFFEE_VERIFY_OK')

def package():
 verification=rt.read_json(OUT/'verification.json')
 if verification['commit']!=rt.project_state()['commit']:raise RuntimeError('package requires current verification')
 # Existing package builder has already linked and checked the actual static UI host.
 source=ROOT/'out/device/imx6ul'
 dest=OUT/'device/coffee-demo';dest.mkdir(parents=True,exist_ok=True)
 shutil.copy2(source/'ui-host-imx6ul-static',dest/'ui-host-imx6ul-static');(dest/'ui-host-imx6ul-static').chmod(0o755)
 (dest/'assets').mkdir(exist_ok=True)
 for name in ('coffee.js','labels.atlas','builtin.rgba','Noto-LICENSE.txt','IMAGE-LICENSE.txt'):
  shutil.copy2(OUT/'assets'/name,dest/'assets'/name)
 shutil.copytree(OUT/'updates',dest/'updates',dirs_exist_ok=True)
 shutil.copy2(ROOT/'scripts/device/run-coffee-demo.sh',dest/'run-coffee-demo.sh');(dest/'run-coffee-demo.sh').chmod(0o755)
 shutil.copy2(ROOT/'docs/coffee-demo.md',dest/'README.md')
 run(['go','build','-trimpath','-ldflags=-s -w','-o',dest/'mediactl','.'],OUT/'media-build-arm.log',cwd=ROOT/'tools/media',env={**os.environ,'CGO_ENABLED':'0','GOOS':'linux','GOARCH':'arm','GOARM':'7'})
 run(['arm-linux-gnueabihf-readelf','-h','-l','-d',dest/'mediactl'],OUT/'media-elf.log')
 elf=(OUT/'media-elf.log').read_text()
 if 'INTERP' in elf or 'NEEDED' in elf:raise RuntimeError('media executable is not static')
 # Prove the exact deployment binary performs a real signed install under ARM emulation.
 temp=OUT/'arm-package-store'
 run(['qemu-arm','-cpu','cortex-a7',dest/'mediactl','install',temp,dest/'updates/demo.public',dest/'updates/demo-b.zip'],OUT/'media-package-smoke.log')
 run(['go','build','-trimpath','-ldflags=-s -w','-o',OUT/'mediactl-windows-amd64.exe','.'],OUT/'media-build-windows.log',cwd=ROOT/'tools/media',env={**os.environ,'CGO_ENABLED':'0','GOOS':'windows','GOARCH':'amd64'})
 rt.write_json(dest/'manifest.json',{'source_commit':rt.project_state()['commit'],'runtime_verification':rt.digest(ROOT/'out/device/imx6ul/manifest.json'),'coffee_verification':rt.digest(OUT/'verification.json'),'ui_elf_sha256':rt.digest(dest/'ui-host-imx6ul-static'),'mediactl_sha256':rt.digest(dest/'mediactl'),'physical_coffee_tested':False,'data_update_only':True,'key_note':'Ephemeral demo public key only. Generate your own keys for your own assets.'})
 files=[p for p in dest.rglob('*') if p.is_file() and p.name!='SHA256SUMS']
 (dest/'SHA256SUMS').write_text(''.join(f'{rt.digest(p)}  {p.relative_to(dest)}\n' for p in sorted(files)))
 with tarfile.open(OUT/'imx6ul-coffee-demo.tar.gz','w:gz') as t:t.add(dest,arcname='coffee-demo')
 # Never include source OTF/TTF/TTC, signing private keys, or the font cache.
 print('COFFEE_PACKAGE_OK sha256='+rt.digest(OUT/'imx6ul-coffee-demo.tar.gz'))

if __name__=='__main__':
 try:
  action=sys.argv[1]
  if action=='test':test(sys.argv[2])
  elif action=='media-test':media_test()
  elif action=='verify':verify()
  elif action=='package':package()
  else:raise RuntimeError('unknown coffee command')
 except Exception as e:print('COFFEE_FAILED: '+str(e),file=sys.stderr);sys.exit(1)
