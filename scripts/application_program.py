#!/usr/bin/env python3
"""Build the application VM against the existing locked QuickJS, not a new engine."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

import port
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/application-program'


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
def run(args,log,expected=0,timeout=180):
    args=list(map(str,args));print('+',' '.join(args),flush=True)
    result=subprocess.run(args,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
    log.parent.mkdir(parents=True,exist_ok=True);log.write_text('$ '+' '.join(args)+'\n'+result.stdout+f'\nexit={result.returncode}\n')
    print(result.stdout,end='',flush=True)
    if result.returncode!=expected:raise RuntimeError(f'command failed: {log}')
    return result.stdout


def state():
    if git('status','--porcelain','--untracked-files=all'):raise RuntimeError('clean committed source required')
    return {'commit':git('rev-parse','HEAD'),'tree':git('rev-parse','HEAD^{tree}'),
        'source_sha256':{n:digest(ROOT/n) for n in git('ls-files').splitlines() if (ROOT/n).is_file()}}


def test(mode):
    before=state();lock=port.configs();source=port.checked_sources(lock)
    # Read the exact same QuickJS directory as P0/P1, keeping its lock and ABI.
    out=OUT/mode;out.mkdir(parents=True,exist_ok=True);(out/'result.json').unlink(missing_ok=True)
    cc='arm-linux-gnueabihf-gcc' if mode=='arm' else 'gcc'
    arch=['-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard'] if mode=='arm' else []
    sanitize=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g'] if mode=='sanitize' else []
    common=['-O2','-fno-strict-aliasing','-fwrapv',*arch,*sanitize]
    objects=[]
    for name in lock['quickjs']['sources']:
        obj=out/(Path(name).stem+'.o');objects.append(obj)
        run([cc,'-std=gnu11','-D_GNU_SOURCE','-DCONFIG_VERSION="'+lock['quickjs']['version']+'"',*common,'-I',source,'-c',source/name,'-o',obj],out/(name+'.log'),timeout=240)
    binary=out/'application-program-test'
    run([cc,'-std=c11','-Wall','-Wextra','-Werror','-Wpedantic',*common,
        '-I.','-isystem',source,'hosts/linux/application/program.c','tests/application/test_program.c',*objects,
        *(['-static'] if mode=='arm' else []),'-lm','-ldl','-lpthread','-o',binary],out/'build.log')
    runner=[] if mode!='arm' else ['qemu-arm','-cpu','cortex-a7']
    outputs=[]
    for name in ('test','repeat','negative'):
        text=run([*runner,binary,*(['--intentional-failure'] if name=='negative' else [])],out/(name+'.log'),1 if name=='negative' else 0,timeout=90)
        if name=='negative':
            if 'INTENTIONAL_PROGRAM_ASSERTION_FAILURE' not in text or 'PROGRAM_OK' in text:raise RuntimeError('negative gate failed open')
        elif 'PROGRAM_OK real-quickjs isolated-program-core only' not in text:raise RuntimeError('missing completed real VM tests')
        else:outputs.append(text)
    if outputs[0]!=outputs[1]:raise RuntimeError('non-deterministic program tests')
    if before!=state():raise RuntimeError('source changed during test')
    report={**before,'scope':'isolated-application-program-only','mode':mode,'real_quickjs':True,
        'application_separation':False,'physical_hardware':False,'sanitized_quickjs_and_program':mode=='sanitize',
        'binary_sha256':digest(binary),'logs':{p.name:digest(p) for p in out.glob('*.log')},
        'quickjs':lock['quickjs'],'dependency_sha256':{p.name:digest(p) for p in source.iterdir() if p.is_file()}}
    (out/'result.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    if mode=='native':
        # Review support only, never copied to the device package. Preserve
        # authentic source/headers and notices for independent native rebuilds.
        support=OUT/'dependencies';support.mkdir(exist_ok=True)
        for p in source.iterdir():
            if p.is_file():shutil.copy2(p,support/p.name)
    print('APPLICATION_PROGRAM_TEST_OK',mode)


def verify():
    current=state();reports=[json.loads((OUT/m/'result.json').read_text()) for m in ('native','arm','sanitize')]
    for report in reports:
        mode=report['mode'];p=OUT/mode
        if any(report[k]!=current[k] for k in current):raise RuntimeError('stale program source evidence')
        if not report['real_quickjs'] or report['application_separation'] or report['physical_hardware']:raise RuntimeError('scope overclaim')
        if digest(p/'application-program-test')!=report['binary_sha256']:raise RuntimeError('changed program binary')
        for n,h in report['logs'].items():
            if digest(p/n)!=h:raise RuntimeError('changed test log')
    for p in (OUT/'dependencies').iterdir():
        if digest(p)!=reports[0]['dependency_sha256'][p.name]:raise RuntimeError('review dependency mismatch')
    # Full stdout must be independent of native/ARM representation.
    outputs=[(OUT/r['mode']/'test.log').read_text().split('\n',1)[1] for r in reports]
    if outputs[0]!=outputs[1] or outputs[0]!=outputs[2]:raise RuntimeError('cross-target results differ')
    (OUT/'verification.json').write_text(json.dumps({**current,'status':'passed','scope':'application-program-core-only',
        'application_separation':False,'physical_hardware':False,'reports':{r['mode']:digest(OUT/r['mode']/'result.json') for r in reports}},indent=2,sort_keys=True)+'\n')
    run(['git','archive','--format=tar','-o',OUT/'source.tar','HEAD'],OUT/'source-archive.log')
    print('APPLICATION_PROGRAM_VERIFIED native arm sanitize; not full application separation')


def main():
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['test','verify']);parser.add_argument('--mode',choices=['native','arm','sanitize'],default='native');args=parser.parse_args()
    if args.action=='test':test(args.mode)
    else:verify()

if __name__=='__main__':
    try:main()
    except Exception as exc:
        print('APPLICATION_PROGRAM_FAILED:',exc,file=sys.stderr);sys.exit(1)
