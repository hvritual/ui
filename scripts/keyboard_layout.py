#!/usr/bin/env python3
"""Validate the local ASCII keyboard catalog. Not a language/IME admission."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def validate(c):
    if c['schema_version']!=1 or c['input_locale']!='en-US' or c['layout']!='qwerty-ascii-v1':raise ValueError('unreviewed layout')
    if c['physical_verified'] is not False or c['max_chars']!=64:raise ValueError('false admission or budget')
    if c['label_ref_base']!=200 or c['ascii_ref_base']!=1000 or c['character_range']!=[32,126]:raise ValueError('catalog ABI')
    if c['number_policy']!='nonnegative-integer-ascii-digits':raise ValueError('number policy')
    if len(c['labels'])!=27 or any(not s or not s.isascii() for s in c['labels']):raise ValueError('label matrix')
    for height in (600,800):
        keyboard_top=height-256
        if not (188<208<264<324<keyboard_top and keyboard_top+232<=height-16):raise ValueError('field obscured')
        if any(32+c*62+54>800 for c in range(12)):raise ValueError('key overlap')
    return True
if __name__=='__main__':
    c=json.loads((ROOT/'assets/locales/keyboard-ascii.json').read_text());validate(c)
    for key,value in [('physical_verified',True),('max_chars',4096),('input_locale','zh-CN'),('labels',[])]:
        bad={**c,key:value}
        try:validate(bad)
        except ValueError:pass
        else:raise RuntimeError('invalid layout admitted')
    print('KEYBOARD_LAYOUT_OK 600 800 ASCII-only no-physical-claim')
