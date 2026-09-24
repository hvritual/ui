#!/usr/bin/env python3
"""Build and validate the isolated decoder and real PocketJS standby host."""
from __future__ import annotations
import hashlib, importlib.util, json, os, shutil, struct, subprocess, sys, tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/video'
RUNTIME=Path(os.environ.get('VIDEO_DEV_RUNTIME',str(ROOT/'out/runtime')))
DEV='VIDEO_DEV_RUNTIME' in os.environ
FLAGS=['-std=c11','-Wall','-Wextra','-Werror','-O2','-Ihosts/linux']
SOURCES=['surface','worker','plan','standby']
OBJECTS=['host','platform','display-presenter','display-fbdev','input-state','input-live','input-bridge','media-store','personality']
WRAPS=['poll','input_live_discover','input_live_close','input_live_reconnect','input_live_drain','fbdev_open','fbdev_close','fbdev_present','host_turn_contacts']
UNIT_CASES={'video-surface-bounds-session-and-contain','video-wake-consumes-whole-gesture-pause-and-priority','video-wire-partial-pts-backpressure-late-drop-stale-session','video-stalled-decoder-bounded-timeout'}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,data):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(data,indent=2)+'\n')
def run(args,log,expected=0,cwd=ROOT,env=None,timeout=180,**kwargs):
 args=list(map(str,args));log=Path(log);log.parent.mkdir(parents=True,exist_ok=True)
 r=subprocess.run(args,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env,timeout=timeout,**kwargs)
 with log.open('a') as f:f.write('$ '+' '.join(args)+'\n'+r.stdout+f'\nexit_code={r.returncode}\n')
 if r.returncode!=expected:raise RuntimeError(f'exit={r.returncode} expected={expected}; see {log}: {r.stdout[-3000:]}')
 print(r.stdout,end='');return r.stdout

def build(mode):
 d=OUT/mode;d.mkdir(parents=True,exist_ok=True);arm=mode=='arm';cc='arm-linux-gnueabihf-gcc' if arm else 'gcc'
 f=FLAGS+(['-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard'] if arm else [])
 prefix=OUT/'deps'/mode
 manifest=json.loads((OUT/'deps'/f'{mode}.json').read_text())
 if manifest['sha256']!='de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f' or manifest['gpl'] or manifest['network']:raise RuntimeError('decoder dependency scope')
 for name,h in manifest['libraries'].items():
  if sha(prefix/'lib'/name)!=h:raise RuntimeError('decoder dependency drift')
 run([cc,*f,'-I'+str(prefix/'include'),'-c','hosts/linux/video/decoder.c','-o',d/'decoder.o'],d/'build.log')
 run([cc,*f,*(['-static'] if arm else []),d/'decoder.o','-L'+str(prefix/'lib'),'-lavformat','-lavcodec','-lswscale','-lavutil','-lm','-o',d/'video-decoder'],d/'build.log')
 run([cc,*f,*['hosts/linux/video/'+s+'.c' for s in SOURCES],'tests/video/test_video.c','-o',d/'video-unit'],d/'build.log')
 include=RUNTIME/f'source-{mode}/engine/quickjs-c';f+=['-I'+str(include)]
 objs=[RUNTIME/mode/(s+'.o') for s in OBJECTS]
 libs=[RUNTIME/mode/'libquickjs.a',RUNTIME/'cargo'/('armv7-unknown-linux-gnueabihf' if arm else 'x86_64-unknown-linux-gnu')/'release/libpocketjs_symbian_core.a','-lm','-ldl','-lpthread','-lrt']
 video=['hosts/linux/video/'+s+'.c' for s in SOURCES]
 for name,main,test in [('video-host','hosts/linux/video/main.c',False),('video-core','tests/video/test_runtime.c',True)]:
  run([cc,*f,*(['-static'] if arm and not test else []),main,*video,*(['hosts/linux/video/cli.c'] if not test else []),*objs,RUNTIME/mode/('runtime-test.o' if test else 'runtime-host.o'),*libs,'-o',d/name],d/'build.log')
 if not arm:
  run([cc,*f,'tests/video/test_loop.c','hosts/linux/video/cli.c',*video,*objs,RUNTIME/mode/'runtime-test.o',*libs,*['-Wl,--wrap='+s for s in WRAPS],'-o',d/'video-loop'],d/'build.log')
  run([cc,*f,'tests/video/test_worker.c',*video,'-o',d/'video-worker-test'],d/'build.log')
  run([cc,*FLAGS,'-O1','-g','-fsanitize=address,undefined',*video,'tests/video/test_video.c','-o',d/'video-sanitized'],d/'build.log')
 run([('arm-linux-gnueabihf-readelf' if arm else 'readelf'),'-h','-A','-l','-d',d/'video-decoder'],d/'decoder.elf.log')
 if arm:
  for name in ('video-host','video-decoder'):
   elf=run(['arm-linux-gnueabihf-readelf','-h','-A','-l','-d',d/name],d/f'{name}.elf.log')
   if 'Requesting program interpreter' in elf or 'NEEDED' in elf or 'Tag_ABI_VFP_args: VFP registers' not in elf:raise RuntimeError('ARM static ELF gate')


def decode_test(mode,name,w,h,runner):
 binary=OUT/mode/'video-decoder';src=OUT/'examples/videos'/f'{name}.mp4'
 with src.open('rb') as f:
  r=subprocess.run([*runner,str(binary),str(f.fileno()),'777',str(w),str(h),'15','1'],pass_fds=(f.fileno(),),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
 (OUT/mode/f'decode-{name}.log').write_bytes(r.stderr)
 if r.returncode or b'VIDEO_DECODER_OK frames=30' not in r.stderr:raise RuntimeError('real video decode failed: '+repr(r.stderr))
 offset=0;pts=[];hashes=[];pixels=[];eof=False
 while offset<len(r.stdout):
  head=r.stdout[offset:offset+64]
  if len(head)!=64 or head[:8]!=b'PUIVFR1\0':raise RuntimeError('decoder framing')
  kind,ww,hh,stride,n,fmt=struct.unpack_from('<6I',head,8);session,seq,time=struct.unpack_from('<3Q',head,32);offset+=64
  if kind==2:
   if n or seq!=30 or time!=pts[-1]:raise RuntimeError('EOF identity')
   eof=True;break
  if (ww,hh,stride,n,fmt,session,seq)!=(w,h,w*4,w*h*4,1,777,len(pts)+1):raise RuntimeError('frame identity/shape')
  p=r.stdout[offset:offset+n];offset+=n
  if len(p)!=n or set(p[3::4])!={255}:raise RuntimeError('frame length/alpha')
  pts.append(time);hashes.append(hashlib.sha256(p).hexdigest())
  if len(pts) in (1,15,30):pixels.append(p)
 if not eof or offset!=len(r.stdout) or len(pts)!=30 or pts[0]!=0 or any(a>=b for a,b in zip(pts,pts[1:])) or len(set(hashes))<15:raise RuntimeError('not a timed moving video')
 sample=OUT/mode/f'decode-{name}.bgra';sample.write_bytes(b''.join(pixels))
 write(OUT/mode/f'decode-{name}.json',{'source_sha256':sha(src),'decoder_sha256':sha(binary),'frames':30,'pts_us':pts,'frame_sha256':hashes,'sample_sha256':sha(sample),'decoded_width':w,'decoded_height':h,'physical':False})
 with src.open('rb') as f:
  r=subprocess.run([*runner,str(binary),str(f.fileno()),'777','16','16','15','1'],pass_fds=(f.fileno(),),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
 if r.returncode!=1 or r.stdout:raise RuntimeError('dimension admission failed open')
 (OUT/mode/f'decode-{name}-negative.log').write_bytes(r.stderr)
 bad=OUT/mode/'invalid.mp4';bad.write_bytes(b'not a movie')
 with bad.open('rb') as f:
  r=subprocess.run([*runner,str(binary),str(f.fileno()),'777','320','180','15','1'],pass_fds=(f.fileno(),),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
 if r.returncode!=1 or r.stdout:raise RuntimeError('invalid movie failed open')
 (OUT/mode/'invalid-decode.log').write_bytes(r.stderr)
 print(f'VIDEO_DECODE_OK mode={mode} clip={name} frames=30')


def tests(mode):
 d=OUT/mode;runner=['qemu-arm','-cpu','cortex-a7','-L','/usr/arm-linux-gnueabihf'] if mode=='arm' else []
 txt=run([*runner,d/'video-unit'],d/'unit.log')
 cases={s[5:] for s in txt.splitlines() if s.startswith('PASS ')}
 if cases!=UNIT_CASES or 'VIDEO_UNIT_OK cases=4' not in txt:raise RuntimeError('missing unit cases')
 run([*runner,d/'video-unit','--intentional-failure'],d/'intentional-failure.log',expected=1)
 txt=run([*runner,d/'video-core',OUT/'assets',d/'home.ppm'],d/'core.log')
 if 'VIDEO_CORE_OK cases=3 real_core=true' not in txt:raise RuntimeError('Core gate missing')
 if mode=='native':run([d/'video-sanitized'],d/'sanitizer.log',env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'})
 for name,w,h in [('motion',320,180),('detail',640,360)]:decode_test(mode,name,w,h,runner)
 media=d/'mediactl'
 run(['go','build','-trimpath','-o',media,'.'],d/'go-build.log',cwd=ROOT/'tools/media',env={**os.environ,'CGO_ENABLED':'0',**({'GOOS':'linux','GOARCH':'arm','GOARM':'7'} if mode=='arm' else {})})
 testbin=d/'media-test'
 run(['go','test','-c','-o',testbin,'.'],d/'go-build.log',cwd=ROOT/'tools/media',env={**os.environ,'CGO_ENABLED':'0',**({'GOOS':'linux','GOARCH':'arm','GOARM':'7'} if mode=='arm' else {})})
 text=run([*runner,testbin,'-test.v','-test.run','^TestVideo'],d/'go-tests.log')
 for n in ('TestVideoValidateSignedBoundary','TestVideoInstallRollbackAndPinnedGeneration'):
  if '--- PASS: '+n not in text:raise RuntimeError('Go test not executed')
 store=d/'store';shutil.rmtree(store,ignore_errors=True)
 key=OUT/'updates/video.public'
 for name in ('a','b'):run([*runner,media,'video-install',store,key,OUT/f'updates/video-{name}.zip'],d/'install.log')
 run([*runner,media,'video-rollback',store,key],d/'install.log')
 if mode=='native':
  run([d/'video-worker-test',store,d/'video-decoder',d/'first-video.ppm'],d/'worker.log',timeout=30)
  for n in (0,2):
   for ext in ('json','csv'):(d/f'loop-{n}.{ext}').unlink(missing_ok=True)
   run([d/'video-loop',OUT/'assets',store,d/'video-decoder',d/f'loop-{n}.json',d/f'loop-{n}.csv',str(n)],d/f'loop-{n}.log',timeout=30)
   result=json.loads((d/f'loop-{n}.json').read_text())
   if not result['ok'] or not result['shown'] or result['decoder_unreaped'] or result['faults']:raise RuntimeError('production loop gate')
   if n==0 and result['wake_gestures']!=1:raise RuntimeError('wake integration')
  fault=d/'decoder-exits';shutil.copy2('/bin/false',fault);fault.chmod(0o700)
  for ext in ('json','csv'):(d/f'loop-1.{ext}').unlink(missing_ok=True)
  run([d/'video-loop',OUT/'assets',store,fault,d/'loop-1.json',d/'loop-1.csv','1'],d/'loop-1.log',timeout=30)
  result=json.loads((d/'loop-1.json').read_text())
  if result['ok'] or result['faults']!=1 or result['starts']!=1 or result['decoder_unreaped']:raise RuntimeError('fault fallback/respawn gate')
  text=run(['go','test','-race','-count=1','-v','-run','^TestVideo','.'],d/'race.log',cwd=ROOT/'tools/media',timeout=180)
  if '--- PASS: TestVideo' not in text:raise RuntimeError('race cases missing')
 write(d/'test.json',{'mode':mode,'unit_cases':sorted(UNIT_CASES),'core_cases':3,'clips':['motion','detail'],'decoder_sha256':sha(d/'video-decoder'),'host_sha256':sha(d/'video-host'),'worker_process_integration':mode=='native','physical_video_validated':False,'development_runtime':DEV})


def verify():
 if DEV:raise RuntimeError('development runtime cannot produce release evidence')
 commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
 tracked=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).split(b'\0');sources={p.decode():sha(ROOT/p.decode()) for p in tracked if p}
 summaries={}
 for mode in ('native','arm'):
  m=json.loads((OUT/mode/'test.json').read_text())
  if m['development_runtime'] or m['physical_video_validated'] or m['decoder_sha256']!=sha(OUT/mode/'video-decoder') or m['host_sha256']!=sha(OUT/mode/'video-host'):raise RuntimeError('scope or output drift')
  summaries[mode]=m
 import numpy as np
 pixels={}
 for clip in ('motion','detail'):
  a=json.loads((OUT/'native'/f'decode-{clip}.json').read_text());b=json.loads((OUT/'arm'/f'decode-{clip}.json').read_text())
  if a['pts_us']!=b['pts_us'] or a['source_sha256']!=b['source_sha256']:raise RuntimeError('ARM time/source differs')
  aa=np.frombuffer((OUT/'native'/f'decode-{clip}.bgra').read_bytes(),np.uint8).astype(np.int16)
  bb=np.frombuffer((OUT/'arm'/f'decode-{clip}.bgra').read_bytes(),np.uint8).astype(np.int16)
  diff=np.abs(aa-bb);maximum=int(diff.max());mean=float(diff.mean())
  if maximum>4 or mean>0.3:raise RuntimeError(f'decoded pixel parity exceeds rounding budget: {clip} {maximum} {mean}')
  pixels[clip]={'sampled_frames':[1,15,30],'max_abs_difference':maximum,'mean_abs_difference':mean,'budget_max':4,'budget_mean':0.3}
 if sha(OUT/'native/home.ppm')!=sha(OUT/'arm/home.ppm'):raise RuntimeError('Core restored-home parity')
 report={'commit':commit,'source_files':sources,'tests':summaries,'decode_pixel_comparison':pixels,'physical_video_validated':False,'platforms':'native full worker/CLI + ARM decoder/Core/state; ARM fork-exec on real board pending','one_decoder_lane':True,'audio_enabled':False,'vsync_enabled':False,'pan_enabled':False}
 write(OUT/'verification.json',report)
 print('VIDEO_VERIFIED '+commit)


def package():
 record=json.loads((OUT/'verification.json').read_text());d=OUT/'package/standby-video'
 if d.exists():shutil.rmtree(d)
 d.mkdir(parents=True)
 for name in ('video-host','video-decoder','mediactl'):shutil.copy2(OUT/'arm'/name,d/name);(d/name).chmod(0o755)
 for name in ('assets','updates','examples'):shutil.copytree(OUT/name,d/name)
 (d/'assets/labels.atlas').unlink()
 (d/'assets/FONT-REQUIRED.txt').write_text('Reuse an authorized existing P4 labels.atlas via VIDEO_FONT_ATLAS. No font file is distributed in this package.\n')
 for path,name in [('scripts/device/run-standby-video.sh','run-standby-video.sh'),('docs/standby-video.md','README.md')]:shutil.copy2(ROOT/path,d/name)
 (d/'run-standby-video.sh').chmod(0o755)
 kit=OUT/'decoder-compliance';shutil.rmtree(kit,ignore_errors=True);kit.mkdir()
 for name in ('COPYING.LGPLv2.1','COPYING.LGPLv3','LICENSE.md'):shutil.copy2(OUT/'deps/arm'/name,kit/name)
 shutil.copy2(OUT/'deps/ffmpeg-7.1.5.tar.xz',kit/'ffmpeg-7.1.5.tar.xz')
 shutil.copy2(OUT/'deps/arm.json',kit/'build.json');shutil.copy2(OUT/'arm/decoder.o',kit/'decoder.o')
 for name in ('decoder.c','wire.h'):shutil.copy2(ROOT/'hosts/linux/video'/name,kit/name)
 shutil.copytree(OUT/'deps/arm/lib',kit/'lib')
 (kit/'relink.sh').write_text('#!/bin/sh\nset -eu\narm-linux-gnueabihf-gcc -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -static decoder.o -Llib -lavformat -lavcodec -lswscale -lavutil -lm -o video-decoder\n')
 (kit/'NOTICE.txt').write_text('Decoder only: FFmpeg 7.1.5, unmodified LGPL configuration; no GPL/nonfree codecs or encoder binary shipped. Exact source archive, options, libraries, adapter source and object are included for inspection and relinking. You may modify/relink the supplied adapter and replace the decoder executable using your authorized deployment channel. No warranty. This does not relicense the independent PocketJS UI runtime. Downstream distributors must retain notices, corresponding source/relink materials and satisfy applicable license terms. Codec patent and commercial redistribution review is separate.\n')
 (d/'DECODER-NOTICE.txt').write_text((kit/'NOTICE.txt').read_text())
 for name in ('COPYING.LGPLv2.1','COPYING.LGPLv3','LICENSE.md'):shutil.copy2(kit/name,d/name)
 record={'source_commit':record['commit'],'verification_sha256':sha(OUT/'verification.json'),'binaries':{n:sha(d/n) for n in ('video-host','video-decoder','mediactl')},'static_arm':True,'physical_video_validated':False}
 write(d/'manifest.json',record)
 def sums(folder):
  files=sorted(p for p in folder.rglob('*') if p.is_file() and p.name!='SHA256SUMS')
  (folder/'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.relative_to(folder)}\n' for p in files))
 def archive(folder,target):
  def owner(info):
   info.uid=info.gid=0;info.uname=info.gname='root';return info
  with tarfile.open(target,'w:gz',format=tarfile.PAX_FORMAT) as tar:tar.add(folder,arcname=folder.name,filter=owner)
 sums(d);sums(kit);archive(d,OUT/'imx6ul-standby-video.tar.gz');archive(kit,OUT/'decoder-source-relink-kit.tar.gz')
 print('VIDEO_PACKAGE_OK '+sha(OUT/'imx6ul-standby-video.tar.gz'))


def main():
 action=sys.argv[1]
 if action=='fixtures':
  d=OUT/'native';d.mkdir(parents=True,exist_ok=True)
  run(['go','build','-trimpath','-o',d/'mediactl','.'],d/'go-build.log',cwd=ROOT/'tools/media',env={**os.environ,'CGO_ENABLED':'0'})
  spec=importlib.util.spec_from_file_location('fixtures',ROOT/'scripts/video_fixtures.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);mod.build(str(d/'mediactl'))
 elif action=='build':build(sys.argv[2])
 elif action=='test':tests(sys.argv[2])
 elif action=='verify':verify()
 elif action=='package':package()
 else:raise RuntimeError('video.py fixtures | build native/arm | test native/arm | verify | package')
if __name__=='__main__':
 try:main()
 except Exception as e:print('VIDEO_FAILED: '+str(e),file=sys.stderr);raise
