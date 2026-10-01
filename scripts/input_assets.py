"""Build a bounded input glyph atlas and an explicit offline-input Coffee package.

The glyph inventory is the locked dictionary's full valid-character inventory,
not the words chosen in a test. Only generated atlas bytes enter the package.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
import coffee_assets
import ime
import pui

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/framework'
LABELS=['Lang','English','Simplified Chinese','Loading candidates...','Type pinyin to see candidates',
        'Input unavailable: switch language to retry','Previous','Next','Select input language',
        'Enter','Composition','Input language is independent of display language','#+=','Delete']


def build(*,debug_font: Path|None=None, debug_index: int=2) -> Path:
    vendor=ime.prepare()
    font=debug_font or coffee_assets.fetch_locked(coffee_assets.FONT_PATH,coffee_assets.FONT_BLOB,20*1024*1024)
    index=debug_index if debug_font else 0
    chars=sorted(set((vendor/'data/valid_utf16.txt').read_text(encoding='utf-16')) |
                 {chr(n) for n in range(32,127)},key=ord)
    if not 16000<len(chars)<=17000 or any(ord(c)<32 or ord(c)>65535 for c in chars):
        raise RuntimeError('invalid locked input character inventory')
    metadata=TTFont(str(font),fontNumber=index);cmap=metadata.getBestCmap();metadata.close()
    if any(ord(c) not in cmap for c in chars):raise RuntimeError('source font missing dictionary characters')
    directory=OUT/'applications/coffee-ime';directory.mkdir(parents=True,exist_ok=True)
    source=OUT/'assets'
    for item in source.iterdir():
        if item.is_file():shutil.copy2(item,directory/item.name)
    catalogs=json.loads((directory/'catalog.json').read_text())
    for cat in catalogs:
        cat.update({str(300+n):text for n,text in enumerate(LABELS)})
        cat['205']='Name'
    (directory/'catalog.json').write_text(json.dumps(catalogs,ensure_ascii=True,separators=(',',':'))+'\n')
    font_hash=hashlib.sha256(font.read_bytes()).hexdigest()
    inventory_hash=hashlib.sha256(''.join(chars).encode()).hexdigest()
    cached=OUT/'input-atlas.json'
    target=directory/'input.atlas'
    identity={'font_sha256':font_hash,'inventory_sha256':inventory_hash,'index':index,'cell':[24,30],
              'pixel_size':22,'bake_scale':2,'slot':1,'debug_only':bool(debug_font)}
    if cached.exists() and target.exists() and json.loads(cached.read_text()).get('identity')==identity:
        if hashlib.sha256(target.read_bytes()).hexdigest()!=json.loads(cached.read_text())['sha256']:
            raise RuntimeError('cached input atlas changed')
    else:
        bake=ImageFont.truetype(str(font),44,index=index,layout_engine=ImageFont.Layout.BASIC)
        data=bytearray(struct.pack('<IHH8B',0x41464344,3,len(chars),24,30,24,36,1,0,1,0))
        pixels=bytearray()
        for number,char in enumerate(chars):
            data+=struct.pack('<IHBB',ord(char),number,24,0)
            tile=Image.new('L',(48,60))
            ImageDraw.Draw(tile).text((2,48),char,font=bake,fill=255,anchor='ls')
            tile=tile.resize((24,30),Image.Resampling.LANCZOS)
            pixels+=tile.tobytes()
        data+=pixels
        if len(data)>13*1024*1024:raise RuntimeError('input atlas exceeds budget')
        target.write_bytes(data)
        cached.write_text(json.dumps({'identity':identity,'glyphs':len(chars),'bytes':len(data),
            'sha256':hashlib.sha256(data).hexdigest(),'shaping':False,'bidi':False,
            'inventory_source_sha256':ime.sha(vendor/'data/valid_utf16.txt')},indent=2)+'\n')
    shutil.copy2(vendor/'data/dict_pinyin.dat',directory/'pinyin.dat')
    (directory/'PINYIN-NOTICE.txt').write_text((vendor/'NOTICE').read_text()+'\n'+(vendor/'qt_attribution.json').read_text())
    manifest=pui.load_manifest(ROOT/'contracts/application-package.example.json')
    manifest['app_id']='com.pocket.coffee-ime'
    manifest['capabilities'].append('ui.ime.pinyin')
    manifest['budgets']['asset_bytes']=pui.MAX_BYTES
    package=pui.build(manifest,directory)
    for target_name in pui.TARGETS:pui.verify(package,target_name,allow_unsigned=True)
    packages=OUT/'packages';packages.mkdir(exist_ok=True)
    output=packages/'coffee-ime.pui';output.write_bytes(package)
    (packages/'coffee-ime.manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return output

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--debug-font',type=Path);parser.add_argument('--debug-index',type=int,default=2)
    args=parser.parse_args()
    print(build(debug_font=args.debug_font,debug_index=args.debug_index))
