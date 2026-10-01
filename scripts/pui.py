#!/usr/bin/env python3
"""Deterministic experimental .pui container tools; never execute applications."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct

MAGIC = b'PUI1\r\n\x1a\n'
HEADER = 192
ENTRY = 96
MAX_BYTES = 16 * 1024 * 1024
MAX_HEAP = 8 * 1024 * 1024
SOURCE_LIMIT = 256 * 1024
TARGETS = {'imx6ul-1024x600': 1, 'imx6ul-1024x800': 2}
CAPABILITIES = {'ui.core': 1, 'ui.keyboard.ascii': 2, 'ui.images': 4, 'ui.ime.pinyin': 8}
FILES = {'application.js', 'catalog.json', 'labels.atlas', 'builtin.rgba',
         'alternate.rgba', 'Noto-LICENSE.txt', 'IMAGE-LICENSE.txt',
         'input.atlas', 'pinyin.dat', 'PINYIN-NOTICE.txt'}
IME_FILES = {'input.atlas', 'pinyin.dat', 'PINYIN-NOTICE.txt'}
REQUIRED = {'application.js', 'catalog.json', 'labels.atlas', 'builtin.rgba'}
ROOT_KEYS = {'format_version', 'runtime_api', 'sdk_api', 'targets', 'capabilities',
             'app_id', 'app_version', 'budgets', 'authentication'}

class PackageError(ValueError):
    pass

def require(ok: bool, code: str) -> None:
    if not ok:
        raise PackageError(code)

def integer(value: object, low: int, high: int) -> bool:
    return type(value) is int and low <= value <= high

def exact(value: object, keys: set[str]) -> bool:
    return type(value) is dict and set(value) == keys

def unique(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        require(key not in result, 'PUI_DUPLICATE_JSON_KEY')
        result[key] = value
    return result

def bounded_read(path: Path, limit: int) -> bytes:
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        st = os.fstat(fd)
        require(stat.S_ISREG(st.st_mode) and 0 <= st.st_size <= limit, 'PUI_INPUT_SIZE')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(limit + 1)
        require(len(data) == st.st_size and len(data) <= limit, 'PUI_INPUT_CHANGED')
        return data
    finally:
        os.close(fd)

def load_manifest(path: Path) -> dict:
    try:
        return validate_manifest(json.loads(bounded_read(path, 32768), object_pairs_hook=unique))
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise PackageError('PUI_MANIFEST_JSON') from exc

def mask(value: object, mapping: dict[str, int], code: str) -> int:
    require(type(value) is list and 0 < len(value) <= len(mapping), code)
    require(all(type(x) is str and x in mapping for x in value), code)
    require(len(set(value)) == len(value), code)
    return sum(mapping[x] for x in value)

def validate_manifest(m: object) -> dict:
    require(exact(m, ROOT_KEYS), 'PUI_MANIFEST_FIELDS')
    require(type(m['format_version']) is int and m['format_version'] == 1, 'PUI_VERSION')
    require(exact(m['runtime_api'], {'min', 'max'}), 'PUI_VERSION')
    r = m['runtime_api']
    require(integer(r['min'], 1, 0xffffffff) and integer(r['max'], r['min'], 0xffffffff), 'PUI_VERSION')
    require(integer(m['sdk_api'], 1, 0xffffffff), 'PUI_VERSION')
    mask(m['targets'], TARGETS, 'PUI_TARGET')
    require(mask(m['capabilities'], CAPABILITIES, 'PUI_CAPABILITY') & 1, 'PUI_CAPABILITY')
    require(type(m['app_id']) is str and re.fullmatch(r'[a-z][a-z0-9.-]{0,62}', m['app_id']) is not None, 'PUI_ID')
    require(type(m['app_version']) is str and len(m['app_version']) < 32 and
            re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', m['app_version']) is not None, 'PUI_APP_VERSION')
    require(exact(m['budgets'], {'heap_bytes', 'asset_bytes'}), 'PUI_BUDGET')
    require(integer(m['budgets']['heap_bytes'], 1024*1024, MAX_HEAP) and
            integer(m['budgets']['asset_bytes'], 1, MAX_BYTES), 'PUI_BUDGET')
    require(m['authentication'] == 'none', 'PUI_AUTH_UNSUPPORTED')
    return m

def container_digest(data: bytes | bytearray) -> bytes:
    return hashlib.sha256(data[:160] + data[HEADER:]).digest()

def build(manifest: dict, assets: Path) -> bytes:
    m = validate_manifest(manifest)
    require(not assets.is_symlink() and assets.is_dir(), 'PUI_ASSET_ROOT')
    names = {p.name for p in assets.iterdir()}
    require(REQUIRED <= names <= FILES and len(names) <= 16, 'PUI_FILE_TABLE')
    caps = mask(m['capabilities'], CAPABILITIES, 'PUI_CAPABILITY')
    require(not (caps & 8) or caps & 2, 'PUI_CAPABILITY')
    if caps & 8:
        require(IME_FILES <= names, 'PUI_REQUIRED_FILE')
    else:
        require(not (IME_FILES & names), 'PUI_CAPABILITY')
    table = bytearray()
    payload = bytearray()
    for name in sorted(names):
        limit = SOURCE_LIMIT if name == 'application.js' else 65536 if name == 'catalog.json' else MAX_BYTES
        data = bounded_read(assets/name, limit)
        require(0 < len(data) <= MAX_BYTES-len(payload), 'PUI_BUDGET')
        table += name.encode('ascii').ljust(48, b'\0')
        table += struct.pack('<QQ', len(payload), len(data)) + hashlib.sha256(data).digest()
        payload += data
    require(len(payload) <= m['budgets']['asset_bytes'] and HEADER+len(table)+len(payload) <= MAX_BYTES, 'PUI_BUDGET')
    header = bytearray(HEADER)
    header[:8] = MAGIC
    struct.pack_into('<8I', header, 8, 1, HEADER, m['runtime_api']['min'], m['runtime_api']['max'],
                     m['sdk_api'], mask(m['targets'], TARGETS, 'PUI_TARGET'),
                     mask(m['capabilities'], CAPABILITIES, 'PUI_CAPABILITY'), len(names))
    struct.pack_into('<Q4I', header, 40, len(payload), m['budgets']['heap_bytes'], m['budgets']['asset_bytes'], SOURCE_LIMIT, 0)
    header[64:128] = m['app_id'].encode('ascii').ljust(64, b'\0')
    header[128:160] = m['app_version'].encode('ascii').ljust(32, b'\0')
    data = header + table + payload
    data[160:192] = container_digest(data)
    return bytes(data)

def fixed_text(data: bytes, pattern: str) -> str:
    end = data.find(b'\0')
    require(0 < end < len(data) and not any(data[end:]), 'PUI_FORMAT')
    try:
        text = data[:end].decode('ascii')
    except UnicodeError as exc:
        raise PackageError('PUI_FORMAT') from exc
    require(re.fullmatch(pattern, text) is not None, 'PUI_FORMAT')
    return text

def verify(data: bytes, target: str, *, allow_unsigned: bool = False,
           runtime_api: int = 1, sdk_api: int = 1, capabilities: int = 15,
           max_heap_bytes: int = MAX_HEAP, max_asset_bytes: int = MAX_BYTES) -> dict:
    require(target in TARGETS, 'PUI_ARGUMENT')
    require(len(data) >= HEADER, 'PUI_TRUNCATED')
    require(len(data) <= MAX_BYTES, 'PUI_BUDGET')
    fmt, header, rmin, rmax, sdk, targets, caps, count = struct.unpack_from('<8I', data, 8)
    size, heap, budget, source, auth = struct.unpack_from('<Q4I', data, 40)
    require(data[:8] == MAGIC and header == HEADER and auth == 0, 'PUI_FORMAT')
    require(fmt == 1 and 0 < rmin <= runtime_api <= rmax and sdk == sdk_api, 'PUI_VERSION')
    require(targets and not targets & ~3 and targets & TARGETS[target], 'PUI_TARGET')
    require(caps & 1 and not caps & ~15 and not caps & ~capabilities, 'PUI_CAPABILITY')
    require(1024*1024 <= heap <= min(MAX_HEAP, max_heap_bytes) and 0 < budget <= min(MAX_BYTES, max_asset_bytes)
            and size <= budget and source == SOURCE_LIMIT, 'PUI_BUDGET')
    require(allow_unsigned, 'PUI_UNSIGNED')
    app_id = fixed_text(data[64:128], r'[a-z][a-z0-9.-]{0,62}')
    app_version = fixed_text(data[128:160], r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)')
    require(0 < count <= 16, 'PUI_FILE_TABLE')
    base = HEADER + count*ENTRY
    require(base <= len(data) and size == len(data)-base, 'PUI_TRUNCATED')
    require(container_digest(data) == data[160:192], 'PUI_INTEGRITY')
    cursor = 0
    files = []
    for index in range(count):
        pos = HEADER + index*ENTRY
        name = fixed_text(data[pos:pos+48], r'[A-Za-z0-9_.-]+')
        require(name in FILES and (not files or name > files[-1]['name']), 'PUI_FILE_TABLE')
        offset, length = struct.unpack_from('<QQ', data, pos+48)
        require(offset == cursor and 0 < length <= size-cursor, 'PUI_FILE_TABLE')
        require(not (name == 'application.js' and length > SOURCE_LIMIT) and
                not (name == 'catalog.json' and length > 65536), 'PUI_BUDGET')
        digest = hashlib.sha256(data[base+cursor:base+cursor+length]).digest()
        require(digest == data[pos+64:pos+96], 'PUI_INTEGRITY')
        files.append({'name': name, 'size': length, 'sha256': digest.hex()})
        cursor += length
    require(cursor == size, 'PUI_FILE_TABLE')
    require(REQUIRED <= {f['name'] for f in files}, 'PUI_REQUIRED_FILE')
    names = {f['name'] for f in files}
    require(not (caps & 8) or caps & 2, 'PUI_CAPABILITY')
    if caps & 8:
        require(IME_FILES <= names, 'PUI_REQUIRED_FILE')
    else:
        require(not (IME_FILES & names), 'PUI_CAPABILITY')
    return {'status': 'container-verified', 'format_version': fmt, 'runtime_api': runtime_api,
            'sdk_api': sdk, 'app_id': app_id, 'app_version': app_version, 'files': files,
            'heap_bytes': heap, 'asset_bytes': budget, 'capabilities': caps, 'target': target,
            'authenticated': False, 'executed': False, 'physical_hardware': False}

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    pack = sub.add_parser('build')
    pack.add_argument('--manifest', type=Path, required=True)
    pack.add_argument('--assets', type=Path, required=True)
    pack.add_argument('--output', type=Path, required=True)
    check = sub.add_parser('verify')
    check.add_argument('--input', type=Path, required=True)
    check.add_argument('--target', choices=TARGETS, required=True)
    check.add_argument('--allow-unsigned', action='store_true')
    args = parser.parse_args()
    if args.command == 'build':
        data = build(load_manifest(args.manifest), args.assets)
        with args.output.open('xb') as stream:
            stream.write(data)
        print(json.dumps({'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'executed': False}))
    else:
        print(json.dumps(verify(bounded_read(args.input, MAX_BYTES), args.target, allow_unsigned=args.allow_unsigned), sort_keys=True))

if __name__ == '__main__':
    try:
        main()
    except (OSError, PackageError) as exc:
        raise SystemExit('PUI_FAILED: ' + str(exc))
