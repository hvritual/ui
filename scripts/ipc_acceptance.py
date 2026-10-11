#!/usr/bin/env python3
"""Prove separate providers reach a real packaged Application VM and renderer."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

import framework as f
import ipc
import pui

OUT = f.OUT
CAPS = ['ui.core', 'ipc.query', 'ipc.command', 'ipc.events', 'ipc.state']


def files(directory: Path) -> dict[str, str]:
    return {str(p.relative_to(directory)): f.sha(p) for p in sorted(directory.rglob('*')) if p.is_file()}


class Process:
    def __init__(self, command: list, log: Path):
        self.command = list(map(str, command))
        self.log = log
        self.stream = log.open('w')
        self.stream.write('$ ' + ' '.join(self.command) + '\n')
        self.stream.flush()
        self.process = subprocess.Popen(self.command, cwd=f.ROOT, stdout=self.stream,
                                        stderr=subprocess.STDOUT)

    def ready(self, predicate, seconds: float = 8) -> None:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError('service process exited before readiness: ' + str(self.log))
            if predicate():
                return
            time.sleep(0.01)
        raise RuntimeError('service readiness deadline: ' + str(self.log))

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
                self.stream.close()
                raise RuntimeError('process did not stop on SIGTERM: ' + str(self.log))
        self.stream.close()
        if self.process.returncode != 0:
            raise RuntimeError('service exited unsuccessfully: ' + str(self.log))


def package(directory: Path, name: str, source: str, caps: list[str]) -> Path:
    assets = directory / (name + '-source')
    shutil.copytree(OUT / 'applications/ipc-demo', assets)
    (assets / 'application.js').write_text(source)
    manifest = pui.load_manifest(f.ROOT / 'contracts/application-package.example.json')
    manifest['app_id'] = 'com.pocket.' + name
    manifest['capabilities'] = caps
    manifest['budgets']['heap_bytes'] = 2 * 1024 * 1024
    destination = directory / (name + '.pui')
    payload = pui.build(manifest, assets)
    for target in f.PROFILES:
        pui.verify(payload, target, allow_unsigned=True)
    destination.write_bytes(payload)
    f.write(directory / (name + '.manifest.json'), manifest)
    return destination


def probe_source(channel: str, expected: str) -> str:
    """An ordinary JS client probes a native denial; no mocked native methods."""
    sdk = (f.ROOT / 'sdk/ipc/js/pocket-comm.js').read_text()
    return sdk + '\n' + r'''
(function () {
  'use strict';
  const comm = new PocketComm(), call = __pocketCall;
  const channel = CHANNEL, expected = EXPECTED;
  let began = false, completed = 0, selected = 0, label;
  function finish(result) {
    completed = +(!!result && !result.ok && result.error.code === expected);
    selected = completed ? 10 : 99;
  }
  globalThis.PocketApplication = {
    start(config) {
      call('theme.define',1,2,22,1,0xfffaf8f4,2,0xff332b26);
      call('style.rule',1,1,2,0); call('theme.select',1);
      const root = call('component.create',1,0,0,0,1024,config.height,1,0,0);
      label = call('component.create',2,root,64,100,300,64,1,5200,0);
      call('navigation.push',1,root); return true;
    },
    event() { return 0; },
    tick() {
      if (!began) {
        began = true;
        let task;
        if (channel === 'query') task = comm.query.call('demo.counter.v1','get',{}, {timeoutMs:250});
        else if (channel === 'command') task = comm.command.execute('demo.counter.v1','add',{delta:1},
          {idempotencyKey:'denied-must-not-execute',timeoutMs:250});
        else if (channel === 'events') task = comm.events.subscribe('demo.counter.v1','changed',null,()=>{}).ready;
        else task = comm.states.watch('demo.counter.v1','counter',null,()=>{}).ready;
        task.then(finish,()=>{selected=98;});
      }
      call('component.text',label,5200+selected);
      call('frame.state',selected,0,0,0,0,0,completed,0,0,0,0,0xfffaf8f4,1);
      return true;
    },
    inspect() { return {completed,selected}; }
  };
})();
PocketComm.install(PocketApplication);
'''.replace('CHANNEL', json.dumps(channel)).replace('EXPECTED', json.dumps(expected))


def check_binary(binary: Path, current: dict, development: bool) -> dict:
    ownership = json.loads(binary.with_suffix('.ownership.json').read_text())
    if ownership['consumer'] != 'runtime' or not ownership['runtime_input_policy_passed']:
        raise RuntimeError('IPC Application proof requires the production Framework ELF')
    if ownership['binary_sha256'] != f.sha(binary) or ownership['source_commit'] != current['commit']:
        raise RuntimeError('stale production Framework ELF')
    if ownership['source_tree'] != current['tree'] or (ownership['development'] and not development):
        raise RuntimeError('Framework evidence does not match canonical source')
    for name, digest in ownership['sha256'].items():
        if f.sha(f.ROOT / name) != digest:
            raise RuntimeError('Framework source/dependency changed: ' + name)
    for entry in ownership['inputs'].values():
        if entry['role'] in ('application', 'test-only') or entry['reference_only']:
            raise RuntimeError('business/test code linked into the production UI ELF')
    required = {'hosts/linux/application/application.c', 'hosts/linux/application/program.c',
                'hosts/linux/ipc/wire.c', 'hosts/linux/ipc/client.c', 'hosts/linux/package/loaded.c'}
    if not required <= set(ownership['inputs']):
        raise RuntimeError('actual IPC/Application/package bridge absent from executable')
    if {'hosts/linux/ipc/service.c', 'hosts/linux/ipc/broker.c'} & set(ownership['inputs']):
        raise RuntimeError('provider or broker implementation linked into UI')
    return ownership


def test(mode: str, development: bool = False) -> None:
    if development:
        os.environ['FRAMEWORK_DEVELOPMENT'] = '1'
    current = ipc.source(development)
    f.runtime_check(mode)
    binary = OUT / mode / 'ui-framework'
    check_binary(binary, current, development)
    f.assets()
    binaries = ipc.build(mode, development=development)
    binary_hash = f.sha(binary)
    broker_hash = f.sha(binaries['pocket-ipcd'])
    directory = f.fresh(OUT / mode, 'ipc-processes')
    executable_dir = directory / 'executables'
    executable_dir.mkdir()
    for name in ('pocket-ipcd', 'service-fixture'):
        shutil.copy2(binaries[name], executable_dir / name)
        shutil.copy2(binaries[name].with_suffix('.ownership.json'), executable_dir / (name + '.ownership.json'))
        shutil.copy2(binaries[name].parent / (name + '-build.log'), executable_dir / (name + '-build.log'))
    original = (OUT / 'applications/ipc-demo/application.js').read_text()
    vendor = original.replace("'demo.counter.v1'", "'vendor.example.v1'").replace(
        "{query: 'get', command: 'add', event: 'changed', state: 'counter'}",
        "{query: 'read', command: 'increase', event: 'updated', state: 'values'}")
    if vendor == original or "'vendor.example.v1'" not in vendor or "query: 'read'" not in vendor:
        raise RuntimeError('dynamic service package variant was not constructed')
    packages = {name: package(directory, name, data, CAPS)
                for name, data in (('ipc-example', original), ('ipc-vendor', vendor))}
    results = {}
    process_groups = []

    def ui_case(name: str, path: Path, socket: Path, height: int, selection: int,
                *, capable: bool = True, seconds: int = 3) -> dict:
        destination = directory / name
        f.run([*ipc.runner(mode), binary, '--headless', '--profile', 'imx6ul-1024x' + str(height),
               '--package', path, '--allow-unsigned-package', '--ipc-development-socket', socket,
               '--seconds', str(seconds), '--output', destination], directory / (name + '.log'), timeout=90)
        report = json.loads((destination / 'report.json').read_text())
        if report['commit'] != current['commit'] or not report['ok'] or report['error'] is not None:
            raise RuntimeError('Application VM did not remain healthy: ' + name)
        if report['completed'] != 1 or report['application_selection'] != selection:
            raise RuntimeError('IPC application assertions incomplete: ' + name + ' ' +
                               str({k: report[k] for k in ('application_selection', 'completed', 'ipc_status', 'ipc_messages')}))
        if not report['package_admitted'] or report['package_sha256'] != f.sha(path):
            raise RuntimeError('immutable package identity missing: ' + name)
        if not report['ipc_development'] or report['ipc_command_capable'] != capable:
            raise RuntimeError('IPC host/capability scope incorrect: ' + name)
        if report['physical_io'] or report['visual_validated'] or report['business_commands'] or report['package_authenticated']:
            raise RuntimeError('software demo overclaims production/device admission')
        if report['core_live_bytes_after_close'] or report['ipc_pending']:
            raise RuntimeError('outstanding work or runtime leak: ' + name)
        if f.sha(destination / 'first.ppm') == f.sha(destination / 'last.ppm'):
            raise RuntimeError('IPC result did not reach rendered pixels: ' + name)
        results[name] = {'runtime_sha256': binary_hash, 'package_sha256': f.sha(path),
                         'selection': report['application_selection'], 'completed': report['completed'],
                         'ipc_requests': report['ipc_requests'], 'ipc_messages': report['ipc_messages'],
                         'first': f.sha(destination / 'first.ppm'), 'last': f.sha(destination / 'last.ppm')}
        return report

    for height in (600, 800):
        with tempfile.TemporaryDirectory(prefix='pocket-ipc-') as temporary:
            socket = Path(temporary) / 'broker.sock'
            prefix = ipc.runner(mode)
            broker = Process([*prefix, binaries['pocket-ipcd'], '--socket', socket,
                              '--client-uid', str(os.getuid()), '--service-uid', str(os.getuid())],
                             directory / ('broker-' + str(height) + '.log'))
            services = []
            try:
                broker.ready(socket.exists)
                first = Process([*prefix, binaries['service-fixture'], '--socket', socket,
                                 '--service', 'demo.counter.v1'], directory / ('provider-' + str(height) + '.log'))
                services.append(first)
                first.ready(lambda: 'SERVICE_READY' in first.log.read_text())
                first_report = ui_case('example-' + str(height), packages['ipc-example'], socket, height, 3)
                # Start a new namespace and four new member names while the
                # original broker/provider remain alive. No runtime rebuild.
                second = Process([*prefix, binaries['service-fixture'], '--socket', socket,
                                  '--service', 'vendor.example.v1', '--query', 'read', '--command', 'increase',
                                  '--event', 'updated', '--state', 'values'],
                                 directory / ('vendor-' + str(height) + '.log'))
                services.append(second)
                second.ready(lambda: 'SERVICE_READY' in second.log.read_text())
                second_report = ui_case('vendor-' + str(height), packages['ipc-vendor'], socket, height, 3)
                if any(r['item_count'] != 1 or r['ipc_requests'] < 9 or r['ipc_messages'] < 12
                       for r in (first_report, second_report)):
                    raise RuntimeError('four channel traffic not demonstrated')
                process_groups.append({'profile': 'imx6ul-1024x' + str(height), 'broker_pid': broker.process.pid,
                                       'provider_pids': [p.process.pid for p in services],
                                       'dynamic_registration_after_first_ui': True})
                if height == 600:
                    for channel in ('query', 'command', 'events', 'state'):
                        missing = 'ipc.' + channel
                        caps = [c for c in CAPS if c != missing]
                        path = package(directory, 'denied-' + channel, probe_source(channel, 'UNAUTHORIZED'), caps)
                        ui_case('denied-' + channel, path, socket, height, 10,
                                capable='ipc.command' in caps, seconds=1)
            finally:
                errors = []
                for process in [*reversed(services), broker]:
                    try:
                        process.close()
                    except RuntimeError as exc:
                        errors.append(str(exc))
                if errors:
                    raise RuntimeError('; '.join(errors))
    # A missing broker is an ordinary Result failure. The VM and renderer
    # remain alive, and disposal succeeds without a provider ever connecting.
    with tempfile.TemporaryDirectory(prefix='pocket-ipc-offline-') as temporary:
        path = package(directory, 'ipc-offline', probe_source('query', 'UNAVAILABLE'), CAPS)
        ui_case('offline', path, Path(temporary) / 'missing.sock', 600, 10, seconds=1)
    if f.sha(binary) != binary_hash or f.sha(binaries['pocket-ipcd']) != broker_hash or current != ipc.source(development):
        raise RuntimeError('source or executable changed during independent-process proof')
    record = {**current, 'mode': mode, 'runtime_sha256': binary_hash, 'broker_sha256': broker_hash,
              'provider_sha256': f.sha(binaries['service-fixture']), 'directory': str(directory.relative_to(OUT)),
              'same_binary_dynamic_services': True, 'real_application_vm': True,
              'real_independent_processes': True, 'physical_hardware': False,
              'production_authentication': False, 'process_groups': process_groups,
              'results': results, 'evidence': files(directory)}
    f.write(OUT / mode / 'ipc-proof.json', record)
    print('IPC_FRAMEWORK_OK', mode, 'development=' + str(development), flush=True)


def verify() -> None:
    current = ipc.source(False)
    proofs = []
    for mode in ('native', 'arm'):
        record = json.loads((OUT / mode / 'ipc-proof.json').read_text())
        if any(record.get(k) != v for k, v in current.items()) or record['physical_hardware'] or record['production_authentication']:
            raise RuntimeError('stale or overstated IPC Framework evidence')
        if not all(record[k] for k in ('same_binary_dynamic_services', 'real_application_vm', 'real_independent_processes')):
            raise RuntimeError('IPC Framework acceptance incomplete')
        check_binary(OUT / mode / 'ui-framework', current, False)
        if record['runtime_sha256'] != f.sha(OUT / mode / 'ui-framework') or record['broker_sha256'] != f.sha(ipc.OUT / mode / 'pocket-ipcd'):
            raise RuntimeError('IPC Framework executable identity drift')
        if record['provider_sha256'] != f.sha(ipc.OUT / mode / 'service-fixture') or record['evidence'] != files(OUT / record['directory']):
            raise RuntimeError('IPC provider or raw proof drift')
        if len(record['results']) != 9:
            raise RuntimeError('IPC profile/capability matrix incomplete')
        proofs.append(record)
    for name in proofs[0]['results']:
        for key in ('first', 'last', 'selection', 'completed'):
            if proofs[0]['results'][name][key] != proofs[1]['results'][name][key]:
                raise RuntimeError('IPC native/ARM pixels or semantics differ: ' + name)
    f.write(OUT / 'ipc-verification.json', {**current, 'status': 'passed',
            'scope': 'independent-process-package-Application-VM-renderer-software-only',
            'physical_hardware': False, 'production_authentication': False,
            'proofs': {m: f.sha(OUT / m / 'ipc-proof.json') for m in ('native', 'arm')}})
    print('IPC_FRAMEWORK_VERIFY_OK', flush=True)


if __name__ == '__main__':
    try:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('action', choices=('test', 'verify'))
        parser.add_argument('--mode', choices=('native', 'arm'), default='native')
        parser.add_argument('--development', action='store_true')
        args = parser.parse_args()
        if args.action == 'verify':
            if args.development:
                raise RuntimeError('development evidence is not canonical acceptance')
            verify()
        else:
            test(args.mode, args.development)
    except Exception as exc:
        print('IPC_FRAMEWORK_FAILED:', exc, file=sys.stderr)
        sys.exit(1)
