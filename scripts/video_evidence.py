#!/usr/bin/env python3
"""Bind video build/test/package evidence to exact source and dependencies."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'out/video'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*a):return subprocess.check_output(['git',*a],cwd=ROOT,text=True).strip()
def state():
 if os.environ.get('VIDEO_DEV_RUNTIME') or git('status','--porcelain','--untracked-files=all'):raise RuntimeError('clean canonical source required')
 files=git('ls-files').splitlines()
 return {'commit':git('rev-parse','HEAD'),'tree':git('rev-parse','HEAD^{tree}'),'sources':{f:sha(ROOT/f) for f in files},'run_id':os.environ.get('GITHUB_RUN_ID','manual'),'run_attempt':os.environ.get('GITHUB_RUN_ATTEMPT','1')}
def write(p,d):p.write_text(json.dumps(d,indent=2)+'\n')
def build_state():
 out={}
 for mode in ('native','arm'):
  build=ROOT/'out/runtime'/mode/'build.json';b=json.loads(build.read_text())
  if b.get('commit')!=git('rev-parse','HEAD'):raise RuntimeError('runtime not from this source commit')
  binaries=['video-decoder','video-host','video-core','video-unit']
  out[mode]={'runtime_build_sha256':sha(build),'dependency_manifest_sha256':sha(OUT/'deps'/f'{mode}.json'),'binaries':{n:sha(OUT/mode/n) for n in binaries}}
 return out
def checked():
 original=json.loads((OUT/'build-context.json').read_text())
 if original['source']!=state() or original['build']!=build_state():raise RuntimeError('source/dependency/build changed after build stamp')
 return original
if __name__=='__main__':
 command=sys.argv[1]
 if command=='build':write(OUT/'build-context.json',{'source':state(),'build':build_state()})
 elif command=='tested':
  checked();mode=sys.argv[2]
  paths=sorted(p for p in (OUT/mode).glob('*') if p.is_file())
  write(OUT/f'{mode}-evidence.json',{'build_context_sha256':sha(OUT/'build-context.json'),'files':{str(p.relative_to(OUT)):sha(p) for p in paths}})
 elif command in ('verify','seal'):
  checked()
  for mode in ('native','arm'):
   record=json.loads((OUT/f'{mode}-evidence.json').read_text())
   if record['build_context_sha256']!=sha(OUT/'build-context.json'):raise RuntimeError('test from wrong build')
   for p,h in record['files'].items():
    if sha(OUT/p)!=h:raise RuntimeError('test output drift: '+p)
  if command=='seal':
   v=json.loads((OUT/'verification.json').read_text());v['evidence_binding']={'build_context':sha(OUT/'build-context.json'),'native':sha(OUT/'native-evidence.json'),'arm':sha(OUT/'arm-evidence.json')};write(OUT/'verification.json',v)
 else:raise RuntimeError('unknown evidence command')
 print('VIDEO_EVIDENCE_'+command.upper()+'_OK')
