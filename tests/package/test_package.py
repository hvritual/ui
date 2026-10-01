#!/usr/bin/env python3
"""Exercise the actual C container parser and the independent Python codec."""
from __future__ import annotations
import argparse
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
import pui


def run(command: list[str], expected: int = 0) -> str:
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if result.returncode != expected:
        raise AssertionError(f'command failed ({result.returncode} != {expected}): {command}\n{result.stdout}\n{result.stderr}')
    return result.stdout.strip()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--runner', default='')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    out = args.output.resolve()
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-O2', '-I'+str(ROOT)]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie']
    elif args.runner:
        flags += ['-static', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard']
    binary = out/'package-probe'
    build_command = [args.cc, *flags, str(ROOT/'hosts/linux/package/package.c'),
                     str(ROOT/'tests/package/probe.c'), '-o', str(binary)]
    build_stdout = run(build_command)
    (out/'build.log').write_text(shlex.join(build_command)+'\n'+build_stdout+'\n')
    prefix = shlex.split(args.runner) + [str(binary)]
    transcript = []
    vectors = [b'', b'abc', b'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq', b'a'*1000000]
    vectors += [bytes((i*29+7)%256 for i in range(n)) for n in (1,55,56,63,64,65,127,128,129,1023)]
    known = ['e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
             'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
             '248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1',
             'cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0']
    for i, data in enumerate(vectors):
        path = out/f'hash-{i}.bin'; path.write_bytes(data)
        expected = hashlib.sha256(data).hexdigest()
        if i < len(known):
            assert expected == known[i]
        assert run(prefix+['hash', str(path)]) == expected
    assets = out/'assets'; assets.mkdir()
    for i, name in enumerate(sorted(pui.FILES - {"input.atlas","pinyin.dat","PINYIN-NOTICE.txt"})):
        (assets/name).write_bytes(bytes([65+i])*(i*37+17))
    manifest = pui.load_manifest(ROOT/'contracts/application-package.example.json')
    valid = pui.build(manifest, assets)
    assert valid == pui.build(manifest, assets)
    checked = 0
    def case(name: str, data: bytes, expected: str = 'PUI_OK', mode: str = '600-unsigned') -> None:
        nonlocal checked
        path = out/(name+'.pui');path.write_bytes(data)
        observed = run(prefix+[mode, str(path)], 0 if expected=='PUI_OK' else 1)
        assert observed == expected, (name, expected, observed)
        python_ok = True
        try:
            pui.verify(data, 'imx6ul-1024x800' if mode=='800-unsigned' else 'imx6ul-1024x600',
                       allow_unsigned=mode!='strict', capabilities=1 if mode=='core-only' else 7,
                       max_heap_bytes=1024*1024 if mode=='small-heap' else pui.MAX_HEAP)
        except pui.PackageError:
            python_ok = False
        assert python_ok == (expected=='PUI_OK'), name
        checked += 1;transcript.append(name+' '+observed)
    case('valid-600', valid);case('valid-800', valid, mode='800-unsigned')
    case('unsigned-default', valid, 'PUI_UNSIGNED', 'strict')
    case('capability-policy', valid, 'PUI_CAPABILITY', 'core-only')
    case('heap-policy', valid, 'PUI_BUDGET', 'small-heap')
    def changed(offset: int, value: int, width: int = 4) -> bytes:
        b = bytearray(valid);struct.pack_into('<Q' if width==8 else '<I', b, offset, value)
        b[160:192] = pui.container_digest(b);return bytes(b)
    for name,offset,value,expected in [
        ('format',8,2,'PUI_VERSION'),('header-size',12,191,'PUI_FORMAT'),
        ('runtime-min',16,2,'PUI_VERSION'),('runtime-max',20,0,'PUI_VERSION'),
        ('sdk',24,2,'PUI_VERSION'),('unknown-target',28,4,'PUI_TARGET'),
        ('wrong-target',28,2,'PUI_TARGET'),('unknown-cap',32,17,'PUI_CAPABILITY'),
        ('no-core',32,6,'PUI_CAPABILITY'),('file-count-zero',36,0,'PUI_FILE_TABLE'),
        ('file-count-max',36,17,'PUI_FILE_TABLE'),('heap-small',48,1,'PUI_BUDGET'),
        ('heap-large',48,pui.MAX_HEAP+1,'PUI_BUDGET'),('assets-too-small',52,1,'PUI_BUDGET'),
        ('source-budget',56,1,'PUI_BUDGET'),('auth-future',60,1,'PUI_FORMAT')]:
        case(name,changed(offset,value),expected)
    for n in (0,1,7,8,63,159,160,191,192,len(valid)-1):
        case('truncated-'+str(n),valid[:n],'PUI_TRUNCATED')
    case('trailing',valid+b'x','PUI_TRUNCATED')
    case('payload-overflow',changed(40,0xffffffffffffffff,8),'PUI_BUDGET')
    for name,offset in [('header-corrupt',65),('table-corrupt',pui.HEADER+20),('payload-corrupt',len(valid)-1)]:
        b=bytearray(valid);b[offset]^=1;case(name,bytes(b),'PUI_INTEGRITY')
    for name,offset,value,width in [('offset-gap',pui.HEADER+48,1,8),
        ('offset-overflow',pui.HEADER+48,0xffffffffffffffff,8),('size-zero',pui.HEADER+56,0,8),
        ('size-overflow',pui.HEADER+56,0xffffffffffffffff,8)]:
        case(name,changed(offset,value,width),'PUI_FILE_TABLE')
    b=bytearray(valid);b[-1]^=1;b[160:192]=pui.container_digest(b)
    case('individual-hash',bytes(b),'PUI_INTEGRITY')
    for name in ('../application.js','framework.js','font.ttf'):
        b=bytearray(valid);b[192:240]=name.encode().ljust(48,b'\0');b[160:192]=pui.container_digest(b)
        case('forbidden-'+name.replace('/','-'),bytes(b),'PUI_FILE_TABLE')
    b=bytearray(valid);b[288:336]=b[192:240];b[160:192]=pui.container_digest(b)
    case('duplicate-entry',bytes(b),'PUI_FILE_TABLE')
    for offset, byte in ((64,ord('A')),(127,ord('x')),(128,ord('/')),(159,ord('x'))):
        b=bytearray(valid);b[offset]=byte;b[160:192]=pui.container_digest(b)
        case('invalid-text-'+str(offset),bytes(b),'PUI_FORMAT')
    # Strict JSON/config and filesystem negatives never execute or unpack data.
    invalid = []
    for key, value in [('format_version',True),('sdk_api',1.0),('targets',list(pui.TARGETS)*2),
                       ('capabilities',['ui.core','device.pump']),('authentication','signed'),('unknown',1)]:
        m=copy.deepcopy(manifest);m[key]=value;invalid.append(m)
    for m in invalid:
        try:pui.build(m,assets)
        except pui.PackageError:pass
        else:raise AssertionError('invalid manifest admitted')
    duplicate=out/'duplicate.json';duplicate.write_text('{"x":1,"x":2}')
    try:pui.load_manifest(duplicate)
    except pui.PackageError:pass
    else:raise AssertionError('duplicate JSON key admitted')
    (assets/'framework.js').write_text('throw new Error("must not run")')
    try:pui.build(manifest,assets)
    except pui.PackageError:pass
    else:raise AssertionError('private adapter asset admitted')
    (assets/'framework.js').unlink();original=(assets/'catalog.json').read_bytes();(assets/'catalog.json').unlink()
    (assets/'catalog.json').symlink_to(out/'duplicate.json')
    try:pui.build(manifest,assets)
    except OSError:pass
    else:raise AssertionError('symlink admitted')
    (assets/'catalog.json').unlink();(assets/'catalog.json').write_bytes(original)
    # CLI refuses overwrite and unsigned packages without an explicit opt-in.
    destination=out/'cli.pui'
    build_cli=[sys.executable,str(ROOT/'scripts/pui.py'),'build','--manifest',str(ROOT/'contracts/application-package.example.json'),
               '--assets',str(assets),'--output',str(destination)]
    run(build_cli);assert destination.read_bytes()==valid
    run(build_cli,1);assert destination.read_bytes()==valid
    run([sys.executable,str(ROOT/'scripts/pui.py'),'verify','--input',str(destination),'--target','imx6ul-1024x600'],1)
    # Repeat parser calls in-process against independently mutated byte strings.
    # Sanitized and ARM jobs exercise the deterministic process-level corpus above.
    differential = 0
    if not args.runner and not args.sanitize:
        wrapper=out/'wrapper.c';wrapper.write_text('#include "hosts/linux/package/package.h"\nint check(const void *b,size_t n){PuiPackage o;PuiPolicy p={1,1,1,7,PUI_MAX_HEAP,PUI_MAX_BYTES,1};return pui_validate(b,n,&p,&o);}\n')
        library=out/'package.so'
        run([args.cc,*flags,'-shared','-fPIC',str(ROOT/'hosts/linux/package/package.c'),str(wrapper),'-o',str(library)])
        native=ctypes.CDLL(str(library)).check;native.argtypes=[ctypes.c_void_p,ctypes.c_size_t];native.restype=ctypes.c_int
        rng=random.Random(5603)
        for _ in range(2000):
            b=bytearray(valid)
            for _ in range(rng.randrange(1,5)):
                b[rng.randrange(len(b))]^=rng.randrange(1,256)
            if rng.randrange(2):b[160:192]=pui.container_digest(b)
            ok=True
            try:pui.verify(bytes(b),'imx6ul-1024x600',allow_unsigned=True)
            except pui.PackageError:ok=False
            buf=ctypes.create_string_buffer(bytes(b));assert (native(buf,len(b))==0)==ok
            differential+=1
    (out/'cases.log').write_text('\n'.join(transcript)+'\n')
    try:
        commit=run(['git','-C',str(ROOT),'rev-parse','HEAD'])
        tree=run(['git','-C',str(ROOT),'rev-parse','HEAD^{tree}'])
        dirty=bool(run(['git','-C',str(ROOT),'status','--porcelain','--untracked-files=all']))
    except AssertionError:
        commit=tree=None;dirty=True
    sources=['hosts/linux/package/package.c','hosts/linux/package/package.h','scripts/pui.py',
             'tests/package/probe.c','tests/package/test_package.py','contracts/application-package.example.json']
    report={'scope':'container-codec-and-policy-only','commit':commit,'tree':tree,'development':dirty,
            'native_runtime_integration':False,'executed_application':False,'physical_hardware':False,'authenticated':False,
            'cases':checked,'hash_vectors':len(vectors),'differential_cases':differential,'runner':args.runner,
            'sanitizer':args.sanitize,'compiler':run([args.cc,'--version']).splitlines()[0],
            'source_sha256':{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in sources},
            'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'evidence_sha256':{str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.rglob('*')) if p.is_file()}}
    (out/'report.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print(f'PACKAGE_CODEC_OK cases={checked} sha_vectors={len(vectors)} differential={differential} runtime_integration=false')

if __name__=='__main__':
    main()
