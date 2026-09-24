#!/usr/bin/env python3
"""Generate diagnostic media; never bundle the fixture encoder or signing seed."""
from pathlib import Path
import hashlib, json, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/video'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def build(media):
 d=OUT/'examples';(d/'videos').mkdir(parents=True,exist_ok=True);(d/'images').mkdir(exist_ok=True)
 recipes=[]
 for name,w,h,profile,bframes in [('motion',320,180,'baseline',0),('detail',640,360,'main',2)]:
  args=['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',f'testsrc2=size={w}x{h}:rate=15','-t','2','-c:v','libx264','-preset','medium','-profile:v',profile,'-pix_fmt','yuv420p','-g','15','-bf',str(bframes),'-an','-movflags','+faststart',str(d/'videos'/f'{name}.mp4')]
  subprocess.run(args,check=True,timeout=60);recipes.append(args)
 from PIL import Image,ImageDraw
 im=Image.new('RGB',(320,180),(32,55,44));dr=ImageDraw.Draw(im)
 dr.rectangle((24,24,295,155),outline=(158,198,169),width=4);dr.text((60,80),'VIDEO POSTER / TOUCH TO EXIT',fill=(230,240,235))
 im.save(d/'images/poster.png')
 assets=[dict(id='poster',file='images/poster.png',license='original diagnostic fixture',kind='image',width=320,height=180,fps_num=0,fps_den=0),dict(id='motion',file='videos/motion.mp4',license='generated FFmpeg testsrc2 diagnostic pattern',kind='video',width=320,height=180,fps_num=15,fps_den=1),dict(id='detail',file='videos/detail.mp4',license='generated FFmpeg testsrc2 diagnostic pattern',kind='video',width=640,height=360,fps_num=15,fps_den=1)]
 base=dict(schema=1,app='standby-video',idle_ms=1500,loop=True,poster='poster',assets=assets)
 variants={'a':dict(version='standby-a',items=[dict(asset='motion',hold_ms=0)]),'b':dict(version='standby-b',items=[dict(asset='poster',hold_ms=1000),dict(asset='detail',hold_ms=0),dict(asset='motion',hold_ms=0)]),'once':dict(version='standby-once',loop=False,items=[dict(asset='motion',hold_ms=0)])}
 updates=OUT/'updates';updates.mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory() as temp:
  key=Path(temp)/'signer';subprocess.run([media,'keygen',key],check=True)
  shutil.copy2(str(key)+'.public',updates/'video.public')
  for name,spec in variants.items():
   path=d/f'video-{name}.json';path.write_text(json.dumps({**base,**spec},indent=2)+'\n')
   target=updates/f'video-{name}.zip';target.unlink(missing_ok=True)
   subprocess.run([media,'video-pack',d,path,str(key)+'.private',target],check=True)
 record={'encoder_used_for_fixtures_only':subprocess.check_output(['ffmpeg','-version'],text=True).splitlines()[0],'recipes':recipes,'files':{str(p.relative_to(OUT)):sha(p) for p in sorted(d.rglob('*')) if p.is_file()},'packages':{p.name:sha(p) for p in updates.iterdir() if p.is_file()},'user_product_video':False}
 (OUT/'fixtures.json').write_text(json.dumps(record,indent=2)+'\n')
 (OUT/'assets').mkdir(exist_ok=True)
 source=ROOT/'out/coffee/assets'
 for name in ('coffee.js','labels.atlas','builtin.rgba','Noto-LICENSE.txt','IMAGE-LICENSE.txt'):shutil.copy2(source/name,OUT/'assets'/name)
 return record
if __name__=='__main__':
 import sys
 build(str(Path(sys.argv[1]).resolve()))
