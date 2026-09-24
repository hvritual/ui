#!/usr/bin/env python3
"""Real native decoder, signed installer and live-plan transition tests."""
import csv,hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/video';D=OUT/'native'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args,name):
 r=subprocess.run(list(map(str,args)),cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=45)
 (D/name).write_text('$ '+' '.join(map(str,args))+'\n'+r.stdout+f'\nexit_code={r.returncode}\n')
 print(r.stdout,end='')
 if r.returncode:raise RuntimeError('playlist test failed: '+name)
 return r.stdout
# The production CLI must preserve actual wake input and CPU-present timestamps.
wake=json.loads((D/'loop-0.json').read_text())
rows=list(csv.DictReader((D/'loop-0.csv').open()))
pairs={(int(r['wake_event_ns']),int(r['wake_present_ns'])) for r in rows if int(r['wake_event_ns']) and int(r['wake_present_ns'])}
if len(pairs)!=1 or wake['wake_cpu_submit_samples']!=1:raise RuntimeError('wake timing was not recorded')
a,b=next(iter(pairs))
if b<a or (b-a)//1000!=wake['wake_cpu_submit_max_us']:raise RuntimeError('wake duration not derived from recorded stamps')
if json.loads((D/'loop-2.json').read_text())['wake_cpu_submit_samples']!=0:raise RuntimeError('priority hook miscounted as touch')
print('PASS video-wake-cpu-submit-timing-not-lcd-latency')
binary=D/'video-playlist-test'
run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-O2','-Ihosts/linux',*['hosts/linux/video/'+s+'.c' for s in ('surface','worker','plan','standby')],'tests/video/test_playlist.c','-o',binary],'playlist-build.log')
cases={
 'mixed':'video-mixed-poster-main-and-baseline-playlist-loops',
 'once':'video-single-play-holds-final-frame-without-respawn',
 'update':'video-live-update-deferred-through-wake-safe-apply-and-rollback',
}
for mode,case in cases.items():
 store=D/('playlist-'+mode);shutil.rmtree(store,ignore_errors=True)
 text=run([binary,mode,store,D/'video-decoder',D/'mediactl',OUT/'updates'],'playlist-'+mode+'.log')
 if text.count('PASS '+case)!=1 or 'VIDEO_PLAYLIST_OK real_worker=true physical_video_validated=false' not in text:raise RuntimeError('playlist cases missing')
(D/'playlist-tests.json').write_text(json.dumps({'cases':cases,'wake_timing':True,'binary_sha256':sha(binary),'decoder_sha256':sha(D/'video-decoder'),'mediactl_sha256':sha(D/'mediactl'),'real_worker':True,'physical_video_validated':False},indent=2)+'\n')
