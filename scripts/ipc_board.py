#!/usr/bin/env python3
"""Build and verify the exact static ARM headless IPC diagnostic archive."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import time

import framework as f
import ipc
import ipc_acceptance as acceptance
import ipc_board_verify as evidence
import pui

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/ipc-board'
ARCHIVE = 'imx6ul-ipc-headless.tar.gz'
PACKAGE_ROOT = 'pocket-ipc-board'


def fresh(stem: str) -> Path:
    path = OUT / (stem + '-' + str(time.time_ns()))
    path.mkdir(parents=True)
    return path


def archive_members(archive: tarfile.TarFile) -> list[tarfile.TarInfo]:
    """Validate the complete member table before extracting any bytes."""
    members = archive.getmembers()
    names = set()
    total = 0
    evidence.require(bool(members), 'empty diagnostic archive')
    for member in members:
        path = PurePosixPath(member.name)
        evidence.require(not path.is_absolute() and '..' not in path.parts and '\\' not in member.name and
                         path.parts and path.parts[0] == PACKAGE_ROOT and
                         path.as_posix() not in names and (member.isdir() or member.isfile()) and
                         not member.mode & 0o6000, 'unsafe/duplicate diagnostic archive member')
        names.add(path.as_posix())
        evidence.require(0 <= member.size <= 64 * 1024 * 1024, 'diagnostic archive member too large')
        total += member.size
    evidence.require(total <= 256 * 1024 * 1024 and PACKAGE_ROOT in names,
                     'diagnostic archive size/root invalid')
    return members


def unpack_archive(path: Path, destination: Path) -> Path:
    evidence.require(not destination.exists() and not destination.is_symlink(), 'archive destination must be new')
    with tarfile.open(path, 'r:gz') as archive:
        members = archive_members(archive)
        destination.mkdir(parents=True)
        for member in members:
            output = destination / member.name
            if member.isdir():
                output.mkdir(parents=True, exist_ok=True)
            else:
                output.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, output.open('xb') as target:
                    shutil.copyfileobj(source, target)
                output.chmod(member.mode & 0o777)
    return destination / PACKAGE_ROOT


def archive_hashes(path: Path) -> dict[str, str]:
    with tarfile.open(path, 'r:gz') as archive:
        members = archive_members(archive)
        result = {}
        for member in members:
            if member.isfile():
                digest = hashlib.sha256()
                with archive.extractfile(member) as source:
                    for block in iter(lambda: source.read(1024 * 1024), b''):
                        digest.update(block)
                result[PurePosixPath(member.name).relative_to(PACKAGE_ROOT).as_posix()] = digest.hexdigest()
        return result


def bind_archive(archive: Path, unpacked: Path) -> None:
    actual = evidence.file_hashes(unpacked)
    actual['SHA256SUMS'] = evidence.sha(unpacked / 'SHA256SUMS')
    evidence.require(archive_hashes(archive) == actual, 'tested extraction differs from final archive')


def check_sources(bundle: Path, current: dict) -> None:
    evidence.require(evidence.read_json(bundle / 'evidence/source.json') == current, 'stale packaged source record')
    for name in evidence.BINARY_NAMES:
        ownership = evidence.read_json(bundle / 'evidence' / (name + '.ownership.json'))
        for filename, digest in ownership['sha256'].items():
            path = (ROOT / filename).resolve()
            evidence.require(not Path(filename).is_absolute() and path.is_relative_to(ROOT.resolve()) and
                             evidence.sha(path) == digest, 'packaged source/dependency drift: ' + filename)


def prerequisites() -> tuple[dict, dict]:
    current = ipc.source(False)
    f.package_check()
    acceptance.verify()
    evidence.require(acceptance.CASES == evidence.CASES, 'IPC diagnostic case contracts diverged')
    normal = acceptance.verify_record('arm', current)
    evidence.require(current == ipc.source(False), 'source changed during package prerequisites')
    return current, normal


def assemble(destination: Path, current: dict, normal: dict, binaries: dict[str, Path]) -> None:
    destination.mkdir(parents=True)
    metadata = destination / 'evidence'
    metadata.mkdir()
    ui = f.OUT / 'arm/ui-framework-static'
    acceptance.check_binary(ui, current, False)
    framework_package = evidence.read_json(f.OUT / 'package.json')
    evidence.require(evidence.sha(ui) == framework_package['static_binary_sha256'] ==
                     evidence.sha(f.OUT / 'device/coffee-framework/ui-framework'),
                     'IPC package does not use the verified deployed UI ELF')
    ui_abi = OUT / ('ui-static-abi-' + str(time.time_ns()) + '.log')
    text = ipc.run(['arm-linux-gnueabihf-readelf', '-h', '-A', '-l', '-d', ui], ui_abi)
    ipc.check_static_arm_abi(text, 'ui-framework')
    paths = {'ui-framework': ui, 'pocket-ipcd': binaries['pocket-ipcd'],
             'service-fixture': binaries['service-fixture']}
    for name, path in paths.items():
        shutil.copy2(path, destination / name)
        (destination / name).chmod(0o755)
        shutil.copy2(path.with_suffix('.ownership.json'), metadata / (name + '.ownership.json'))
        shutil.copy2(path.parent / (path.name + '-build.log'), metadata / (name + '-build.log'))
        shutil.copy2(ui_abi if name == 'ui-framework' else path.parent / (name + '-elf.log'),
                     metadata / (name + '-elf.log'))
    (destination / 'packages').mkdir()
    original = f.OUT / normal['directory']
    for package_name in sorted(evidence.PACKAGE_NAMES):
        source = original / package_name
        payload = source.read_bytes()
        for profile in evidence.PROFILES:
            admitted = pui.verify(payload, profile, allow_unsigned=True)
            evidence.require(admitted['app_id'] == 'com.pocket.' + package_name.removesuffix('.pui') and
                             admitted['capabilities'] == evidence.caps_for(package_name.removesuffix('.pui')) and
                             admitted['heap_bytes'] == 2 * 1024 * 1024, 'diagnostic application package mapping differs')
        shutil.copy2(source, destination / 'packages' / package_name)
    for copied, original_name in evidence.SOURCE_COPIES.items():
        shutil.copy2(ROOT / original_name, destination / copied)
    (destination / 'run-ipc.sh').chmod(0o755)
    shutil.copy2(ROOT / 'contracts/runtime-ownership.json', metadata / 'runtime-ownership.json')
    evidence.write_json(metadata / 'source.json', current)
    build = evidence.read_json(binaries['pocket-ipcd'].parent / 'build.json')
    evidence.require(build.get('static') is True and build.get('mode') == 'arm' and
                     build.get('sanitizer') is False and all(build.get(key) == value for key, value in current.items()),
                     'static IPC build identity invalid')
    locked = evidence.read_json(ROOT / 'toolchains/sources.lock.json')['toolchain']['gcc_major']
    evidence.require(locked == 11 and isinstance(build.get('gcc'), str) and
                     int(build['gcc'].split('.')[0]) == locked, 'locked static GCC version missing')
    for name in ('pocket-ipcd', 'service-fixture'):
        evidence.require(build['binaries'][name] == evidence.sha(destination / name) and
                         build['ownership'][name] == evidence.sha(metadata / (name + '.ownership.json')) and
                         build['logs'][name + '-build.log'] == evidence.sha(metadata / (name + '-build.log')) and
                         build['abi_logs'][name + '-elf.log'] == evidence.sha(metadata / (name + '-elf.log')),
                         'static IPC build receipt differs')
    evidence.write_json(metadata / 'toolchain.json', {'gcc_major': locked, 'gcc_version': build['gcc'],
                         'target': 'armv7-linux-gnueabihf', 'link': 'static',
                         'board_libc_loader_admitted': False})
    evidence.write_json(destination / 'manifest.json', {
        'schema': 1, 'source_commit': current['commit'], 'source_tree': current['tree'],
        'scope': evidence.SCOPE, 'profiles': evidence.PROFILES, 'static': True, 'headless': True,
        'physical_hardware': False, 'business_commands': False, 'production_authentication': False,
        'development': False, 'independent_board_review': 'pending',
        'binaries': {name: evidence.sha(destination / name) for name in evidence.BINARY_NAMES},
        'packages': {name: evidence.sha(destination / 'packages' / name) for name in sorted(evidence.PACKAGE_NAMES)},
        'cases': evidence.case_contracts(),
        'software_proof_location': 'separate artifact; bound to final archive SHA256',
    })
    checksums = evidence.file_hashes(destination)
    (destination / 'SHA256SUMS').write_text(''.join(digest + '  ' + name + '\n'
                                                  for name, digest in checksums.items()))
    evidence.verify_bundle(destination)
    check_sources(destination, current)


def run_launcher(bundle: Path, report: Path, log: Path, emulator: Path) -> None:
    command = [str(bundle / 'run-ipc.sh'), '--emulator', str(emulator), '--output', str(report)]
    print('+', shlex.join(command), flush=True)
    process = subprocess.Popen(command, cwd=bundle, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output, _ = process.communicate(timeout=1200)
    except BaseException:
        process.terminate()
        try:
            output, _ = process.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate()
        log.write_text('$ ' + shlex.join(command) + '\n' + output + '\nexit=' + str(process.returncode) + '\n')
        raise
    log.write_text('$ ' + shlex.join(command) + '\n' + output + '\nexit=' + str(process.returncode) + '\n')
    print(output, end='', flush=True)
    evidence.require(process.returncode == 0, 'shipped IPC launcher failed: ' + str(log))


def compare_software(result: dict, normal: dict) -> None:
    for name in evidence.CASES:
        for key in ('first', 'last', 'selection', 'completed'):
            evidence.require(result['results'][name][key] == normal['results'][name][key],
                             'static/dynamic IPC software pixels or semantics differ: ' + name + '/' + key)


def package() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name in ('verification.json', 'package.json', ARCHIVE, ARCHIVE + '.sha256'):
        (OUT / name).unlink(missing_ok=True)
    current, normal = prerequisites()
    binaries = ipc.build('arm', static=True)
    stage = fresh('staging') / PACKAGE_ROOT
    assemble(stage, current, normal, binaries)
    archive = OUT / ARCHIVE
    with tarfile.open(archive, 'w:gz') as output:
        output.add(stage, arcname=PACKAGE_ROOT)
    archive_digest = evidence.sha(archive)
    (OUT / (ARCHIVE + '.sha256')).write_text(archive_digest + '  ' + ARCHIVE + '\n')
    inspection = OUT / ('inspection-' + str(time.time_ns()))
    bundle = unpack_archive(archive, inspection)
    evidence.verify_bundle(bundle)
    bind_archive(archive, bundle)
    emulator_name = shutil.which('qemu-arm')
    evidence.require(emulator_name is not None, 'qemu-arm is required for exact static package proof')
    emulator = Path(emulator_name).resolve()
    report = OUT / ('proof-' + str(time.time_ns()))
    launcher_log = OUT / (report.name + '-runner.log')
    run_launcher(bundle, report, launcher_log, emulator)
    result = evidence.verify_run(bundle, report, expected_mode='qemu')
    compare_software(result, normal)
    evidence.require(current == ipc.source(False) and evidence.sha(archive) == archive_digest,
                     'source/archive changed during static diagnostic proof')
    record = {**current, 'archive': ARCHIVE, 'archive_sha256': archive_digest,
              'manifest_sha256': evidence.sha(bundle / 'manifest.json'),
              'tested_package': str(bundle.relative_to(OUT)), 'report': str(report.relative_to(OUT)),
              'launcher_log': launcher_log.name, 'launcher_log_sha256': evidence.sha(launcher_log),
              'run_verification_sha256': evidence.sha(report / 'verification.json'),
              'framework_package_sha256': evidence.sha(f.OUT / 'package.json'),
              'ipc_framework_verification_sha256': evidence.sha(f.OUT / 'ipc-verification.json'),
              'emulator': str(emulator), 'emulator_sha256': evidence.sha(emulator),
              'physical_hardware': False, 'production_authentication': False}
    evidence.write_json(OUT / 'package.json', record)
    verify()
    print('IPC_BOARD_PACKAGE_OK', archive_digest, 'physical_hardware=false', flush=True)


def record_path(record: dict, field: str, pattern: str) -> Path:
    relative = record.get(field)
    evidence.require(isinstance(relative, str) and re.fullmatch(pattern, relative) is not None,
                     'invalid package proof path: ' + field)
    path = OUT / relative
    evidence.require(path.resolve().is_relative_to(OUT.resolve()) and not path.is_symlink(),
                     'escaped package proof path: ' + field)
    return path


def verify() -> None:
    (OUT / 'verification.json').unlink(missing_ok=True)
    current, normal = prerequisites()
    record = evidence.read_json(OUT / 'package.json')
    evidence.require(all(record.get(key) == value for key, value in current.items()) and
                     record.get('development') is False and record.get('physical_hardware') is False and
                     record.get('production_authentication') is False and record.get('archive') == ARCHIVE,
                     'stale/overstated diagnostic package evidence')
    archive = OUT / ARCHIVE
    evidence.require(evidence.sha(archive) == record.get('archive_sha256') and
                     (OUT / (ARCHIVE + '.sha256')).read_text() == record['archive_sha256'] + '  ' + ARCHIVE + '\n',
                     'diagnostic archive identity differs')
    bundle = record_path(record, 'tested_package', r'inspection-[0-9]+/pocket-ipc-board')
    report = record_path(record, 'report', r'proof-[0-9]+')
    log = record_path(record, 'launcher_log', r'proof-[0-9]+-runner\.log')
    evidence.require(log.name == report.name + '-runner.log', 'launcher/report association differs')
    manifest = evidence.verify_bundle(bundle)
    check_sources(bundle, current)
    bind_archive(archive, bundle)
    evidence.require(evidence.sha(bundle / 'manifest.json') == record.get('manifest_sha256') and
                     evidence.sha(f.OUT / 'package.json') == record.get('framework_package_sha256') and
                     evidence.sha(f.OUT / 'ipc-verification.json') == record.get('ipc_framework_verification_sha256'),
                     'diagnostic prerequisite/manifest drift')
    text = evidence.check_log(log, 'IPC_BOARD_COLLECTED mode=qemu')
    emulator = evidence.absolute_record_path(record.get('emulator'))
    evidence.require(evidence.sha(log) == record.get('launcher_log_sha256') and
                     evidence.sha(Path(emulator)) == record.get('emulator_sha256') and
                     shlex.split(text.splitlines()[0][2:]) ==
                     [str(bundle / 'run-ipc.sh'), '--emulator', emulator, '--output', str(report)],
                     'exact unpacked launcher proof differs')
    result = evidence.verify_run(bundle, report, expected_mode='qemu')
    run = evidence.read_json(report / 'run.json')
    evidence.require(run['package_directory'] == str(bundle) and run['output_directory'] == str(report) and
                     run['emulator'] == emulator and result['emulator_sha256'] == record.get('emulator_sha256'),
                     'runner did not execute the recorded extraction/emulator')
    evidence.require(evidence.sha(report / 'verification.json') == record.get('run_verification_sha256'),
                     'raw static proof differs')
    compare_software(result, normal)
    evidence.require(current == ipc.source(False), 'source changed during diagnostic verification')
    evidence.write_json(OUT / 'verification.json', {
        **current, 'status': 'passed', 'scope': 'final-archive-unpacked-static-qemu-headless',
        'physical_hardware': False, 'physical_io': False, 'business_commands': False,
        'production_authentication': False, 'independent_board_review': 'pending',
        'archive': ARCHIVE, 'archive_sha256': record['archive_sha256'],
        'package_record_sha256': evidence.sha(OUT / 'package.json'),
        'manifest_sha256': record['manifest_sha256'], 'binaries': manifest['binaries'],
        'packages': manifest['packages'], 'run_verification_sha256': record['run_verification_sha256'],
        'results': result['results'],
    })
    print('IPC_BOARD_PACKAGE_VERIFIED physical_hardware=false', flush=True)


if __name__ == '__main__':
    try:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('action', choices=('package', 'verify'))
        arguments = parser.parse_args()
        package() if arguments.action == 'package' else verify()
    except Exception as exc:
        print('IPC_BOARD_PACKAGE_FAILED:', exc, file=sys.stderr)
        sys.exit(1)
