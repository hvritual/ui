"""Build the locked standalone decoder for the existing fixed native runtime."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import subprocess
import ime


def build(mode: str, sanitize: bool = False) -> Path:
    vendor = ime.prepare()
    directory = ime.OUT / ('runtime-' + ('sanitize' if sanitize else mode))
    directory.mkdir(parents=True, exist_ok=True)
    compiler = 'arm-linux-gnueabihf-g++' if mode == 'arm' else 'g++'
    flags = ['-std=c++11', '-O2', '-fno-strict-aliasing', '-I.', '-I' + str(vendor/'include')]
    if mode == 'arm':
        flags += ['-mcpu=cortex-a7', '-mfpu=neon-vfpv4', '-mfloat-abi=hard']
    if sanitize:
        flags += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    source = [ime.ROOT/'hosts/linux/ime/pinyin.cpp', *sorted((vendor/'share').glob('*.cpp'))]
    inputs = {str(p.relative_to(ime.ROOT)): ime.sha(p) for p in source}
    inputs.update({str(p.relative_to(ime.ROOT)): ime.sha(p) for p in sorted((vendor/'include').glob('*.h'))})
    # The local API/header and package digest API also affect this translation unit.
    for n in ('hosts/linux/ime/pinyin.h', 'hosts/linux/package/package.h', 'hosts/linux/text-input/session.h'):
        inputs[n] = ime.sha(ime.ROOT/n)
    spec = {'inputs': inputs, 'compiler': subprocess.check_output([compiler,'--version'],text=True), 'flags': flags,
            'lock_sha256': ime.sha(ime.LOCK)}
    archive = directory/'libpocket_ime.a'
    receipt = directory/'build.json'
    if receipt.exists():
        old = json.loads(receipt.read_text())
        if old['spec'] == spec:
            if not archive.is_file() or ime.sha(archive) != old['archive_sha256']:
                raise RuntimeError('cached IME archive changed')
            return archive
    objects = []
    for index, path in enumerate(source):
        obj = directory/f'{index}.o'
        strict = ['-Wall','-Wextra','-Werror','-Wpedantic'] if index == 0 else []
        ime.run([compiler,*flags,*strict,'-c',path,'-o',obj],directory/f'compile-{index}.log')
        objects.append(obj)
    archive.unlink(missing_ok=True)
    ime.run(['ar','rcsD',archive,*objects],directory/'archive.log')
    ime.write(receipt, {'spec':spec,'archive_sha256':ime.sha(archive),
                       'objects':{p.name:ime.sha(p) for p in objects}, 'qt_linked':False})
    return archive
