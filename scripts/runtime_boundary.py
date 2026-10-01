#!/usr/bin/env python3
"""Classify actual compiler/link inputs; this is not runtime or board acceptance."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ROLES = frozenset({'runtime', 'application', 'adapter', 'target', 'test-only'})
CONSUMERS = frozenset({'reference', 'reference-test', 'runtime', 'runtime-test'})
COMPILERS = frozenset({'gcc', 'arm-linux-gnueabihf-gcc'})
LINK_SUFFIXES = frozenset({'.c', '.cc', '.cpp', '.cxx', '.s', '.S', '.o', '.a', '.so'})


class BoundaryError(ValueError):
    """An input has no admitted ownership or crosses an explicit boundary."""


def relative(path: str | Path, root: Path, *, must_exist: bool = True) -> str:
    root = root.resolve()
    p = Path(path)
    if not p.is_absolute():
        p = root / p
    try:
        resolved = p.resolve(strict=must_exist)
        name = resolved.relative_to(root).as_posix()
    except (OSError, ValueError, RuntimeError) as exc:
        raise BoundaryError('input escapes checkout or is missing: ' + str(path)) from exc
    if must_exist and not resolved.is_file():
        raise BoundaryError('input is not a regular file: ' + name)
    if not re.fullmatch(r'[A-Za-z0-9_./-]+', name) or '..' in PurePosixPath(name).parts:
        raise BoundaryError('unsupported path syntax: ' + name)
    return name


def read_policy(root: Path = ROOT) -> dict:
    path = root / 'contracts/runtime-ownership.json'
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise BoundaryError('duplicate manifest key: ' + key)
            result[key] = value
        return result
    policy = json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique)
    if type(policy.get('schema_version')) is not int or policy['schema_version'] != 1:
        raise BoundaryError('unsupported ownership schema')
    entries = policy.get('link_inputs')
    if not isinstance(entries, dict) or not entries:
        raise BoundaryError('empty ownership manifest')
    for name, entry in entries.items():
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_./-]+', name):
            raise BoundaryError('invalid ownership path')
        p = PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or str(p) != name:
            raise BoundaryError('noncanonical ownership path: ' + name)
        if not isinstance(entry, dict) or set(entry) != {'role', 'reference_only', 'origin'} or entry['role'] not in ROLES:
            raise BoundaryError('invalid ownership entry: ' + name)
        if type(entry['reference_only']) is not bool or not isinstance(entry['origin'], str) or not entry['origin']:
            raise BoundaryError('invalid ownership metadata: ' + name)
        if name.startswith('apps/') and entry['role'] != 'application':
            raise BoundaryError('application reclassified as runtime: ' + name)
        if name.startswith('tests/') and entry['role'] != 'test-only':
            raise BoundaryError('test reclassified as runtime: ' + name)
        if name.startswith('out/') and not entry['origin'].startswith('locked:'):
            raise BoundaryError('generated input lacks locked provenance: ' + name)
    payloads = policy.get('private_payloads', {})
    if not payloads or any(v != 'adapter-not-developer-asset' for v in payloads.values()):
        raise BoundaryError('private adapter ownership missing')
    return policy


def admit(name: str, policy: dict, consumer: str) -> dict:
    if consumer not in CONSUMERS:
        raise BoundaryError('unknown build consumer')
    entry = policy['link_inputs'].get(name)
    if entry is None:
        raise BoundaryError('unclassified link input: ' + name)
    if not consumer.endswith('-test') and entry['role'] == 'test-only':
        raise BoundaryError('test input in deployment: ' + name)
    if consumer.startswith('runtime') and (entry['role'] == 'application' or entry['reference_only']):
        raise BoundaryError('application/reference input in fixed runtime: ' + name)
    return entry


def parse_command(command: list[str], root: Path, policy: dict, consumer: str) -> tuple[list[str], list[str], str]:
    if not command or Path(command[0]).name not in COMPILERS:
        raise BoundaryError('unrecognized compiler invocation')
    if consumer not in CONSUMERS:
        raise BoundaryError('unknown build consumer')
    inputs, preprocessor = [], [command[0], '-MM']
    system_includes = []
    output = None
    expect = None
    for arg in command[1:]:
        if expect:
            if expect == '-o':
                if output is not None:
                    raise BoundaryError('multiple compiler outputs')
                output = relative(arg, root, must_exist=False)
            else:
                raise BoundaryError('unexpected split compiler option')
            expect = None
            continue
        if arg == '-o':
            expect = arg
            continue
        if arg.startswith('@') or any(x in arg for x in ('-fplugin', '-include', '-imacros', '-Xlinker', '--script')):
            raise BoundaryError('opaque compiler input rejected: ' + arg)
        if not arg.startswith('-'):
            name = relative(arg, root)
            if Path(name).suffix not in LINK_SUFFIXES:
                raise BoundaryError('unrecognized compiler input type: ' + name)
            admit(name, policy, consumer)
            if name in inputs:
                raise BoundaryError('duplicate compiler input: ' + name)
            inputs.append(name)
            continue
        if arg.startswith(('-I', '-isystem')):
            prefix = '-isystem' if arg.startswith('-isystem') else '-I'
            if len(arg) == len(prefix):
                raise BoundaryError('use a single explicit include-path argument')
            directory = Path(arg[len(prefix):])
            if not directory.is_absolute():
                directory = root / directory
            try:
                local_dir = directory.resolve(strict=True).relative_to(root.resolve()).as_posix()
            except (OSError, ValueError) as exc:
                raise BoundaryError('include directory escapes checkout') from exc
            if not directory.is_dir():
                raise BoundaryError('include path is not a directory')
            # Native compilation treats pinned dependency headers as system
            # headers. For the ownership audit, put the same paths after every
            # ordinary -I path, preserving lookup order while making -MM expose
            # their transitive application/test imports instead of hiding them.
            if prefix == '-isystem' and not local_dir.startswith('out/'):
                raise BoundaryError('system include must be a locked output dependency')
            if prefix == '-isystem':
                system_includes.append('-I'+arg[len(prefix):])
            else:
                preprocessor.append(arg)
        elif arg.startswith(('-D', '-U', '-std=', '-mcpu=', '-mfpu=', '-mfloat-abi=')):
            preprocessor.append(arg)
        elif arg in ('-Wall', '-Wextra', '-Werror', '-Wpedantic', '-O2', '-g', '-fno-omit-frame-pointer', '-static', '-Wl,--gc-sections', '-lm', '-lstdc++', '-ldl', '-lpthread', '-lrt', '-fsanitize=address,undefined'):
            pass
        elif arg.startswith('-Wl,--wrap=') and consumer.endswith('-test') and re.fullmatch(r'-Wl,--wrap=[A-Za-z_][A-Za-z0-9_]*', arg):
            pass
        else:
            raise BoundaryError('unreviewed compiler/linker option: ' + arg)
    if expect or output is None or not inputs or not any(Path(n).suffix == '.c' for n in inputs):
        raise BoundaryError('incomplete compiler/link invocation')
    if output in inputs or not output.startswith('out/'):
        raise BoundaryError('compiler output must be separate under out/')
    return inputs, preprocessor+system_includes, output


def import_admit(owner: dict, dependency: str, policy: dict, consumer: str) -> None:
    """Use compiler-resolved dependencies, not a regex approximation of C includes."""
    if Path(dependency).suffix in LINK_SUFFIXES:
        child = admit(dependency, policy, consumer)
        role, reference_only = child['role'], child['reference_only']
    else:
        role = ('application' if dependency.startswith('apps/') else
                'test-only' if dependency.startswith('tests/') else 'runtime')
        reference_only = dependency in policy.get('reference_headers', [])
    if role == 'test-only' and not consumer.endswith('-test'):
        raise BoundaryError('test include in deployment: ' + dependency)
    if consumer.startswith('runtime') and (role == 'application' or reference_only):
        raise BoundaryError('application/reference include in fixed runtime: ' + dependency)
    if owner['role'] in {'runtime', 'target'} and not owner['reference_only'] and (role in {'application', 'test-only'} or reference_only):
        raise BoundaryError('platform source imports application/test: ' + dependency)


def validate_command(command: list[str], root: Path = ROOT, consumer: str = 'reference') -> dict:
    policy = read_policy(root)
    inputs, preprocessor, output = parse_command(list(map(str, command)), root, policy, consumer)
    imports = {}
    for name in inputs:
        if Path(name).suffix != '.c':
            continue
        owner = admit(name, policy, consumer)
        result = subprocess.run([*preprocessor, str(root / name)], cwd=root, text=True, capture_output=True, timeout=60)
        if result.returncode:
            raise BoundaryError('dependency preprocessing failed: ' + name + '\n' + result.stderr)
        flat = result.stdout.replace('\\\n', ' ')
        if ':' not in flat:
            raise BoundaryError('missing compiler dependency output: ' + name)
        dependencies = [relative(p, root) for p in shlex.split(flat.split(':', 1)[1])]
        if name not in dependencies:
            raise BoundaryError('compiler omitted primary source dependency')
        for dep in dependencies:
            import_admit(owner, dep, policy, consumer)
        imports[name] = sorted(set(dependencies))
    hashed = sorted(set(inputs) | {p for deps in imports.values() for p in deps})
    return {'schema_version': 1, 'consumer': consumer, 'compiler': Path(command[0]).name,
            'output': output, 'ownership_manifest_sha256': hashlib.sha256((root/'contracts/runtime-ownership.json').read_bytes()).hexdigest(),
            'inputs': {n: policy['link_inputs'][n] for n in inputs}, 'compiler_dependencies': imports,
            'sha256': {n: hashlib.sha256((root/n).read_bytes()).hexdigest() for n in hashed},
            'scope': 'link-input-ownership-only', 'runtime_input_policy_passed': consumer.startswith('runtime'),
            'application_separation': False, 'physical_hardware': False}


def check_checkout(root: Path = ROOT) -> dict:
    policy = read_policy(root)
    for name in policy['link_inputs']:
        if not name.startswith('out/'):
            relative(name, root)
    for name in policy['private_payloads']:
        if not name.startswith('out/'):
            relative(name, root)
    make = (root/'Makefile').read_text()
    if 'python3 scripts/framework.py ' in make or 'python3 scripts/framework_guard.py ' not in make:
        raise BoundaryError('canonical framework Make targets must use the guarded builder')
    return {'status': 'passed', 'scope': 'ownership-inventory-only', 'application_separation': False,
            'physical_hardware': False, 'declared_link_inputs': len(policy['link_inputs'])}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check'])
    parser.parse_args()
    print(json.dumps(check_checkout(), sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except (BoundaryError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as exc:
        print('RUNTIME_BOUNDARY_REJECTED:', exc, file=sys.stderr)
        sys.exit(1)
