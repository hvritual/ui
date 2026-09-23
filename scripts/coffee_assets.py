#!/usr/bin/env python3
"""Build bounded, licensed static text and replaceable image fixtures for P4."""
from __future__ import annotations
import hashlib, json, os, shutil, struct, subprocess, sys, tempfile, unicodedata
from pathlib import Path
from urllib.request import urlopen
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/coffee'
FONT_REV='523d033d6cb47f4a80c58a35753646f5c3608a78'
FONT_PATH='Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf'
FONT_BLOB='dc15562470b4f842321894787a0d066879ccff8b'
LICENSE_BLOB='d952d62c065f3f35fb83a173496e90b21525aef3'

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def git_blob(b): return hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
def fetch_locked(path, blob, limit):
    cache=ROOT/'.cache/coffee-font'/Path(path).name
    cache.parent.mkdir(parents=True,exist_ok=True)
    if not cache.exists():
        url=f'https://raw.githubusercontent.com/notofonts/noto-cjk/{FONT_REV}/{path}'
        with urlopen(url,timeout=60) as r: data=r.read(limit+1)
        if len(data)>limit or git_blob(data)!=blob: raise RuntimeError('font source integrity')
        cache.write_bytes(data)
    if git_blob(cache.read_bytes())!=blob: raise RuntimeError('cached font integrity')
    return cache

def generate_atlas(path, locales, *, debug_index=0):
    # A bounded static-label atlas, NOT arbitrary-input CJK or a shaping engine.
    text=json.dumps(locales,ensure_ascii=False)+''.join(chr(n) for n in range(32,127))
    chars=sorted(set(text)-set('\n\r\t'), key=ord)
    f=TTFont(path,fontNumber=debug_index); cmap=f.getBestCmap()
    missing=[c for c in chars if ord(c) not in cmap]
    f.close()
    if missing: raise RuntimeError('font missing: '+repr(missing))
    if len(chars)>512: raise RuntimeError('glyph budget')
    font=ImageFont.truetype(str(path),22,index=debug_index,layout_engine=ImageFont.Layout.BASIC)
    cell=(32,36); header=struct.pack('<IHH8B',0x41464344,3,len(chars),*cell,26,36,0,0,1,0)
    records=[]; pixels=bytearray(); widths={}
    for i,c in enumerate(chars):
        advance=max(1,round(font.getlength(c)))
        if advance>32: raise RuntimeError('advance budget')
        tile=Image.new('L',cell)
        ImageDraw.Draw(tile).text((0,26),c,font=font,fill=255,anchor='ls')
        records.append(struct.pack('<IHBB',ord(c),i,advance,0)); pixels.extend(tile.tobytes())
        widths[c]=advance
    data=header+b''.join(records)+pixels
    if len(data)>600*1024: raise RuntimeError('atlas budget')
    return data,widths

def main():
    import argparse
    p=argparse.ArgumentParser(); p.add_argument('--debug-font'); a=p.parse_args()
    OUT.mkdir(parents=True,exist_ok=True); assets=OUT/'assets'; assets.mkdir(exist_ok=True)
    locales=json.loads((ROOT/'apps/coffee-demo/locales.json').read_text())
    for l in locales.values():
        for value in l.values():
            for s in value if isinstance(value,list) else [value]:
                if unicodedata.normalize('NFC',s)!=s: raise RuntimeError('NFC label required')
    if a.debug_font:
        font=Path(a.debug_font); license_file=None; index=3
    else:
        font=fetch_locked(FONT_PATH,FONT_BLOB,20*1024*1024)
        license_file=fetch_locked('LICENSE',LICENSE_BLOB,16384); index=0
    atlas,widths=generate_atlas(font,locales,debug_index=index)
    (assets/'labels.atlas').write_bytes(atlas)
    script='const COFFEE_LOCALES='+json.dumps(locales,ensure_ascii=False,separators=(',',':'))+';\n'
    script+=(ROOT/'apps/coffee-demo/app.js').read_text()
    (assets/'coffee.js').write_text(script)
    media=OUT/'mediactl';
    subprocess.run(['go','build','-trimpath','-o',str(media),'.'],cwd=ROOT/'tools/media',check=True,env={**os.environ,'CGO_ENABLED':'0'})
    updates=OUT/'updates'; updates.mkdir(exist_ok=True)
    # Ephemeral fixture signing key is never copied into output artifacts.
    with tempfile.TemporaryDirectory() as temp:
        prefix=Path(temp)/'fixture'
        subprocess.run([media,'keygen',prefix],check=True)
        shutil.copy2(str(prefix)+'.public',updates/'demo.public')
        packets=[]
        for name in ('a','b'):
            images=OUT/('images-'+name)
            subprocess.run([media,'fixture',images,name],check=True)
            bundle=updates/('demo-'+name+'.zip')
            if bundle.exists(): bundle.unlink()
            subprocess.run([media,'pack',images,'demo-'+name,str(prefix)+'.private',bundle],check=True)
            store=Path(temp)/('store-'+name)
            subprocess.run([media,'install',store,updates/'demo.public',bundle],check=True)
            generation=(store/'current').read_text().splitlines()[0]
            packet=assets/('builtin.rgba' if name=='a' else 'alternate.rgba')
            shutil.copy2(store/(generation+'.rgba'),packet)
            packets.append({'variant':name,'generation':generation,'packet_sha256':sha(packet),'bundle_sha256':sha(bundle)})
    if license_file: shutil.copy2(license_file,assets/'Noto-LICENSE.txt')
    (assets/'IMAGE-LICENSE.txt').write_text('The supplied cup silhouettes are original diagnostic fixtures authored in tools/media/media.go. They are not production product photography. Replacement material must be supplied with authorization.\n')
    max_names={k:max(sum(widths[c] for c in s) for s in v['names']) for k,v in locales.items()}
    if max(max_names.values())>270: raise RuntimeError('drink label exceeds layout budget')
    manifest={'schema':1,'application':'coffee-demo','font_source':{'repository':'notofonts/noto-cjk','revision':FONT_REV,'path':FONT_PATH,'git_blob':FONT_BLOB,'sha256':sha(font),'debug_only':bool(a.debug_font)},'atlas':{'glyph_count':len(widths),'bytes':len(atlas),'sha256':sha(assets/'labels.atlas'),'pixel_size':22,'cell':[32,36],'static_labels_only':True},'locale_max_drink_label_px':max_names,'ui_locales':list(locales),'input_locale':None,'keyboard_layout':None,'shaping':False,'bidi':False,'image_decoders':['PNG','JPEG'],'image_texture':[256,144],'image_slots':8,'image_fixture_only':True,'updates':packets,'files':{p.name:sha(p) for p in sorted(assets.iterdir()) if p.is_file()}}
    (OUT/'assets.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n')
    print('COFFEE_ASSETS_OK glyphs='+str(len(widths))+' atlas_bytes='+str(len(atlas)))
if __name__=='__main__': main()
