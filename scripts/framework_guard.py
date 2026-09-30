#!/usr/bin/env python3
"""Run the unchanged reference builder with ownership checks at its compiler seam."""
from __future__ import annotations
import functools
import hashlib
import json
from pathlib import Path
import sys

import framework
from runtime_boundary import BoundaryError, COMPILERS, validate_command


def install() -> None:
    original_compile = framework.compile_binary

    @functools.wraps(original_compile)
    def compile_guarded(mode, test=False, sanitize=False, static=False, loop=False):
        consumer = 'reference-test' if test or loop else 'reference'
        original_run = framework.run
        records = []

        def guarded_run(args, log, expected=0, timeout=900):
            command = list(map(str, args))
            if command and Path(command[0]).name in COMPILERS:
                records.append(validate_command(command, framework.ROOT, consumer))
            return original_run(args, log, expected, timeout)

        framework.run = guarded_run
        try:
            binary = original_compile(mode, test, sanitize, static, loop)
        finally:
            framework.run = original_run
        if len(records) != 1:
            raise BoundaryError('reference builder did not expose exactly one compiler invocation')
        record = records[0]
        if (framework.ROOT/record['output']).resolve() != binary.resolve():
            raise BoundaryError('compiled output differs from declared binary')
        record.update({'source_commit': framework.git('rev-parse', 'HEAD'),
                       'source_tree': framework.git('rev-parse', 'HEAD^{tree}'),
                       'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                       'development': framework.state()['development']})
        binary.with_suffix(binary.suffix+'.ownership.json').write_text(json.dumps(record, indent=2, sort_keys=True)+'\n')
        print('LINK_OWNERSHIP_OK consumer='+consumer+' application_separation=false', flush=True)
        return binary

    framework.compile_binary = compile_guarded


def main() -> None:
    install()
    framework.main()


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('FRAMEWORK_FAILED:', exc, file=sys.stderr)
        sys.exit(1)
