#!/usr/bin/env python3
"""Pinned minimal LGPL FFmpeg libraries; never use host FFmpeg libraries implicitly."""
from __future__ import annotations
import hashlib, json, os, shutil, subprocess, sys, tarfile
from pathlib import Path
from urllib.request import urlopen
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/video/deps'
VERSION='7.1.5'
SHA='de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f'
URL=f'https://ffmpeg.org/releases/ffmpeg-{VERSION}.tar.xz'
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(args,log,cwd=ROOT):
 with open(log,'ab') as f:
  f.write(('$ '+' '.join(map(str,args))+'\n').encode());f.flush()
  subprocess.run(list(map(str,args)),cwd=cwd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=900)
def build(mode):
 if mode not in ('native','arm'):raise ValueError('mode')
 OUT.mkdir(parents=True,exist_ok=True)
 archive=OUT/f'ffmpeg-{VERSION}.tar.xz'
 if not archive.exists():
  with urlopen(URL,timeout=120) as response: raw=response.read(16*1024*1024+1)
  if len(raw)>16*1024*1024 or hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('FFmpeg source checksum')
  archive.write_bytes(raw)
 if digest(archive)!=SHA:raise ValueError('FFmpeg cached source checksum')
 source=OUT/f'ffmpeg-{VERSION}'
 if not source.exists():
  with tarfile.open(archive) as t:
   for m in t.getmembers():
    if Path(m.name).is_absolute() or '..' in Path(m.name).parts or not(m.isfile() or m.isdir()):raise ValueError('FFmpeg source member')
   t.extractall(OUT)
 builddir=OUT/(mode+'-build');builddir.mkdir(exist_ok=True)
 prefix=OUT/mode;prefix.mkdir(exist_ok=True)
 log=OUT/(mode+'-build.log')
 args=[str(source/'configure'),'--prefix='+str(prefix),'--disable-all','--disable-autodetect','--disable-network','--disable-programs','--disable-doc','--disable-debug','--disable-shared','--enable-static','--disable-gpl','--disable-nonfree','--disable-pthreads','--disable-x86asm','--enable-avcodec','--enable-avformat','--enable-avutil','--enable-swscale','--enable-decoder=h264','--enable-parser=h264','--enable-demuxer=mov']
 if mode=='arm':args+=['--enable-cross-compile','--arch=arm','--cpu=cortex-a7','--target-os=linux','--cross-prefix=arm-linux-gnueabihf-','--extra-cflags=-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard']
 run(args,log,builddir);run(['make','-j2'],log,builddir);run(['make','install'],log,builddir)
 for name in ('COPYING.LGPLv2.1','COPYING.LGPLv3','LICENSE.md'):shutil.copy2(source/name,prefix/name)
 report={'version':VERSION,'url':URL,'sha256':SHA,'mode':mode,'configure':args,'compiler':subprocess.check_output([('arm-linux-gnueabihf-gcc' if mode=='arm' else 'gcc'),'--version'],text=True).splitlines()[0],'libraries':{p.name:digest(p) for p in sorted((prefix/'lib').glob('*.a'))},'source_patched':False,'gpl':False,'network':False}
 (OUT/(mode+'.json')).write_text(json.dumps(report,indent=2)+'\n')
 print('VIDEO_DEPS_OK mode='+mode+' sha256='+SHA)
if __name__=='__main__':build(sys.argv[1])
