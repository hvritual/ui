#!/usr/bin/env python3
"""Verify immutable software evidence and rerun ORIGINAL executables, not rebuilds."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile

class EvidenceError(ValueError):
    pass

def need(condition, message):
    if not condition:
        raise EvidenceError(message)

def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def unique(pairs):
    result = {}
    for key, value in pairs:
        need(key not in result, 'duplicate JSON key')
        result[key] = value
    return result

def load(path: Path):
    need(path.is_file() and not path.is_symlink() and path.stat().st_size <= 16*1024*1024, 'invalid JSON evidence file')
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique)

def member(root: Path, name: str) -> Path:
    need(type(name) is str and name and '\\' not in name, 'invalid evidence path')
    p = PurePosixPath(name)
    need(not p.is_absolute() and '..' not in p.parts and p.as_posix() == name and name != '.', 'unsafe evidence path')
    dest = root.joinpath(*p.parts)
    need(not any(x.is_symlink() for x in [dest, *dest.parents] if x != root.parent), 'symlink evidence')
    need(dest.resolve().is_relative_to(root.resolve()), 'evidence escaped root')
    return dest

def check_hash(root: Path, name: str, expected: str):
    need(type(expected) is str and re.fullmatch('[0-9a-f]{64}', expected), 'invalid SHA256')
    path = member(root, name)
    need(path.is_file() and sha(path) == expected, 'hash mismatch: ' + name)

def git(root: Path, *args: str) -> str:
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None

def download_artifact(anchor: dict, baseline: dict, destination: Path):
    """Credentials go only to api.github.com, never to the signed storage URL."""
    token = os.environ.get('GH_TOKEN')
    need(bool(token), 'GH_TOKEN required for artifact read')
    base = 'https://api.github.com/repos/' + baseline['repository'] + '/actions/artifacts/' + str(anchor['id'])
    headers = {'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json', 'User-Agent': 'language-evidence-review'}
    with urllib.request.urlopen(urllib.request.Request(base, headers=headers), timeout=60) as response:
        meta = json.loads(response.read(1024*1024), object_pairs_hook=unique)
    need(meta['id'] == anchor['id'] and not meta['expired'], 'artifact unavailable')
    need(meta['digest'] == 'sha256:' + anchor['sha256'], 'artifact metadata digest mismatch')
    need(meta['workflow_run']['head_sha'] == baseline['commit'] and meta['workflow_run']['id'] == baseline['run_id'], 'wrong artifact source/run')
    try:
        urllib.request.build_opener(NoRedirect).open(urllib.request.Request(base + '/zip', headers=headers), timeout=60)
        raise EvidenceError('expected signed artifact redirect')
    except urllib.error.HTTPError as exc:
        need(exc.code in (301, 302, 303, 307), 'artifact redirect failed')
        location = exc.headers['Location']
        exc.close()
    url = urllib.parse.urlsplit(location)
    host = url.hostname or ''
    need(url.scheme == 'https' and (host.endswith('.blob.core.windows.net') or host.endswith('.githubusercontent.com')), 'unapproved artifact storage')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(location, timeout=120) as response, destination.open('xb') as output:
        total = 0
        while True:
            chunk = response.read(1024*1024)
            if not chunk:
                break
            total += len(chunk)
            need(total <= 512*1024*1024, 'artifact download exceeds bound')
            output.write(chunk)
    need(sha(destination) == anchor['sha256'], 'downloaded archive digest mismatch')
    return {'id': anchor['id'], 'sha256': anchor['sha256'], 'bytes': total}

def extract(archive: Path, target: Path):
    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        entries = z.infolist()
        need(len(entries) <= 50000 and sum(x.file_size for x in entries) <= 8*1024**3, 'artifact extraction budget')
        seen = set()
        for item in entries:
            name = item.filename.rstrip('/')
            path = member(target, name)
            need(name not in seen and not stat.S_ISLNK(item.external_attr >> 16), 'duplicate/symlink ZIP entry')
            seen.add(name)
            need(item.file_size <= 512*1024**2, 'oversized ZIP entry')
            if item.is_dir():
                path.mkdir(parents=True, exist_ok=True)
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            with z.open(item) as source, path.open('xb') as output:
                shutil.copyfileobj(source, output, 1024*1024)
            with path.open('rb') as stream:
                if stream.read(4) == b'\x7fELF':
                    path.chmod(0o755)

def verify_extracted(archive: Path, target: Path):
    """Bind extracted bytes back to the digest-anchored ZIP, not self-written hashes."""
    count=0
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            if item.is_dir():
                continue
            h=hashlib.sha256()
            with z.open(item) as stream:
                for chunk in iter(lambda:stream.read(1024*1024),b''):
                    h.update(chunk)
            check_hash(target,item.filename,h.hexdigest())
            count+=1
    need(count>0,'empty evidence archive')
    return count

def source_identity(root: Path, baseline: dict):
    need(git(root, 'rev-parse', 'HEAD') == baseline['commit'], 'baseline checkout is not the evidence commit')
    need(git(root, 'rev-parse', 'HEAD^{tree}') == baseline['tree'], 'baseline tree changed')
    names = git(root, 'ls-files').splitlines()
    return {'commit': baseline['commit'], 'tree': baseline['tree'],
            'source_files': {n: sha(member(root,n)) for n in names}, 'development': False}

def check_source_report(report: dict, identity: dict):
    need(all(report.get(k) == v for k,v in identity.items()), 'stale/development source report')
    need(report.get('physical_hardware') is False, 'software evidence forged as hardware')
    for key in ('product_language_admitted', 'authenticated', 'production_admission', 'qt_linked'):
        need(report.get(key, False) is False, 'overclaimed software evidence: ' + key)

def record_directory(record: dict) -> str:
    # R2 named this evidence_root; R3/R4 name it directory. Admit one explicit
    # contract, never guess the location or skip its raw evidence.
    names=[record[key] for key in ('directory','evidence_root') if key in record]
    need(len(names)==1 and type(names[0]) is str,'missing/ambiguous evidence root')
    return names[0]

def check_record_files(root: Path, record: dict):
    folder = member(root, record_directory(record))
    actual = {p.relative_to(folder).as_posix(): sha(p) for p in sorted(folder.rglob('*')) if p.is_file()}
    need(actual == record['evidence'], 'raw evidence manifest differs')
    for name, digest in record['evidence'].items():
        check_hash(folder, name, digest)

def image_manifest(directory: Path) -> dict:
    """Preserve viewport subdirectories; equal basenames are different evidence."""
    return {p.relative_to(directory).as_posix():sha(member(directory,p.relative_to(directory).as_posix()))
            for p in sorted(directory.rglob('*.ppm')) if p.is_file()}

def compare_images(directory: Path, expected: dict):
    actual=image_manifest(directory)
    missing=sorted(set(expected)-set(actual));extra=sorted(set(actual)-set(expected))
    different=sorted(n for n in set(actual)&set(expected) if actual[n]!=expected[n])
    need(not missing and not extra and not different,'rerun pixel evidence differs: '+json.dumps({'missing':missing,'extra':extra,'different':different}))
    need(bool(actual),'empty image evidence')

def verify_bundle(source: Path, framework: Path, baseline: dict):
    identity = source_identity(source, baseline)
    total = load(framework/'verification.json')
    check_source_report(total, identity)
    need(total['status'] == 'passed' and total['image_count'] == 42 and total['replay_count'] == 6, 'missing original regression gate')
    need(total['application_separation'] is True and total['offline_input_software'] is True, 'missing real application/input integration')
    iv = load(framework/'input-verification.json')
    check_hash(framework, 'input-verification.json', total['input_verification_sha256'])
    check_source_report(iv, identity)
    need(iv['status'] == 'passed' and iv['rendered_ui'] is True and iv['genuine_offline_provider'] is True and iv['runtime_package_integration'] is True and iv['image_count'] == 28, 'missing rendered input gate')
    receipt = load(framework/'input-atlas.json')
    need(receipt['identity']['debug_only'] is False, 'debug font rejected')
    need(receipt['glyphs'] == 16561 and receipt['bytes'] == 12056424 and receipt['identity']['slot'] == 1 and receipt['identity']['cell'] == [24,30], 'font repertoire/geometry mismatch')
    need(receipt['shaping'] is False and receipt['bidi'] is False, 'unproven shaping/BiDi')
    check_hash(framework, 'applications/coffee-ime/input.atlas', receipt['sha256'])
    records = []
    for mode, san in [('native',False),('arm',False),('native',True)]:
        filename = mode + ('/input-sanitize.json' if san else '/input.json')
        check_hash(framework, filename, iv['reports'][mode + ('-sanitize' if san else '')])
        r = load(framework/filename)
        check_source_report(r, identity)
        need(r['mode'] == mode and r['sanitizer'] is san and len(r['images']) == 28, 'missing target/mode images')
        check_hash(framework, r['binary'], r['binary_sha256'])
        check_hash(framework, 'input-atlas.json', r['font_receipt_sha256'])
        check_hash(framework, 'packages/coffee-ime.pui', r['package_sha256'])
        check_record_files(framework, r)
        compare_images(member(framework,r['directory'])/'cases',r['images'])
        records.append(r)
    need(all(r['images'] == records[0]['images'] for r in records), 'Native/ARM/sanitizer pixel disagreement')
    need(records[0]['production_proof']['results'] == records[1]['production_proof']['results'], 'Native/ARM input behavior disagreement')
    for r in records[:2]:
        proof = r['production_proof']
        check_source_report(proof, identity)
        need(proof['cases'] >= 18 and proof['runtime_package_integration'] is True, 'incomplete production input proof')
        check_hash(framework, r['mode']+'/ui-framework', proof['runtime_sha256'])
        check_record_files(framework, proof)
        need(set(proof['results']) == {'input-600-closed','input-600-private','input-800-closed','input-800-private'}, 'missing target privacy cases')
        for result in proof['results'].values():
            need(result['ime_commits'] == 1 and result['package_sha256'] == iv['package_sha256'], 'wrong package or unproven candidate commit')
    deploy = framework/'device/coffee-framework'
    m = load(deploy/'manifest.json')
    need(m['source_commit'] == baseline['commit'] and m['source_tree'] == baseline['tree'], 'deployed source mismatch')
    need(m['product_language_admitted'] is False and m['physical_hardware'] is False and m['package_authenticated'] is False, 'deployed scope inflation')
    need(m['input_method_locales'] == ['en-US','zh-CN'], 'deployed locale mismatch')
    sums = (deploy/'SHA256SUMS').read_text().splitlines()
    for line in sums:
        digest, name = line.split('  ',1)
        check_hash(deploy, name, digest)
    check_hash(deploy, 'ui-framework', m['binary_sha256'])
    need(sha(framework/'arm/ui-framework-static') == m['binary_sha256'], 'packaged static ELF differs')
    for name,digest in m['packages'].items():
        check_hash(deploy, 'packages/'+name,digest)
    for filename, manifest_key in [('input-proof.json','input_proof_sha256'),('package-proof.json','package_proof_sha256'),('application-proof.json','application_proof_sha256')]:
        check_hash(deploy,filename,m[manifest_key])
        p = load(deploy/filename)
        check_source_report(p,identity)
        need(p['runtime_sha256'] == m['binary_sha256'], 'static proof used another executable')
        check_record_files(framework,p)
    p = load(framework/'package.json')
    check_hash(framework,p['archive'],p['sha256'])
    need(p['static_binary_sha256'] == m['binary_sha256'], 'deployment archive binding mismatch')
    sys.path.insert(0,str(source/'scripts'))
    import pui
    packages = {}
    for name,digest in m['packages'].items():
        blob=(deploy/'packages'/name).read_bytes()
        items=[]
        for target in pui.TARGETS:
            items.append(pui.verify(blob,target,allow_unsigned=True))
        packages[name]={'sha256':digest,'targets':items}
    ime_files={f['name']: f for f in packages['coffee-ime.pui']['targets'][0]['files']}
    contract=load(source/'contracts/offline-input.json')
    need(ime_files['pinyin.dat']['sha256'] == contract['dictionary_sha256'], 'dictionary identity mismatch')
    need(ime_files['input.atlas']['sha256'] == receipt['sha256'], 'font not bound to package')
    return {'status':'verified','basis':identity,'artifacts':baseline,'software_only':True,
        'physical_hardware':False,'product_language_admitted':False,'authenticated':False,
        'packages':packages,'font_receipt':receipt,'runtime_static_sha256':m['binary_sha256'],
        'input_verification_sha256':sha(framework/'input-verification.json'),
        'source_files_checked':len(identity['source_files']),'deployment_checksums':len(sums),
        'input_image_count':28,'legacy_image_count':42,'legacy_replay_count':6}

def rerun_original(source: Path, framework: Path):
    sys.path.insert(0,str(source/'scripts'))
    import framework as f
    import input_acceptance as acceptance
    import package_acceptance
    need(f.OUT.resolve() == framework.resolve(), 'review path differs from original Runtime root')
    folder=framework/'language-independent-rerun'
    need(not folder.exists(), 'refuse overwrite of old review')
    folder.mkdir()
    results={}
    for mode, static, binary in [('native',False,framework/'native/ui-framework'),('arm',True,framework/'arm/ui-framework-static')]:
        proof=acceptance.prove_input_binary(binary,mode,folder/(mode+'-production'),static=static)
        need(proof['cases'] >= 18,'missing original executable cases')
        legacy=package_acceptance.prove_package_binary(binary,mode,folder/(mode+'-legacy-packages'),static=static)
        need(legacy['same_binary_packaged_applications'] is True and legacy['runtime_sha256']==proof['runtime_sha256'],'legacy/new packages did not use the same Runtime')
        results[mode]={'cases':proof['cases'],'legacy_package_cases':legacy['cases'],'runtime_sha256':proof['runtime_sha256'],'package_sha256':proof['package_sha256']}
        (folder/(mode+'-proof.json')).write_text(json.dumps(proof,sort_keys=True,indent=2)+'\n')
        (folder/(mode+'-legacy-proof.json')).write_text(json.dumps(legacy,sort_keys=True,indent=2)+'\n')
    for san in (False,True):
        r=load(framework/('native/input-sanitize.json' if san else 'native/input.json'))
        for label in ('cases','repeat','negative'):
            dest=folder/(('sanitize-' if san else 'native-')+label);dest.mkdir()
            command=[framework/r['binary'],framework/'packages/coffee-ime.pui',dest,framework/'assets']
            if label=='negative':command.append('--intentional-failure')
            text=f.run(command,dest.with_suffix('.log'),1 if label=='negative' else 0,timeout=300)
            if label=='negative':
                need('INTENTIONAL_IME_VIEW_ASSERTION_FAILURE' in text,'wrong deliberate-failure path')
            else:
                compare_images(dest,r['images'])
    results['original_native_view_and_sanitizer']='positive-repeat-negative-passed'
    results['fresh_compile']=False
    results['execution_environment']='GitHub-Actions-independent-audit-job'
    results['physical_hardware']=False
    results['raw_logs']={p.relative_to(folder).as_posix():sha(p) for p in folder.rglob('*.log')}
    return results

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--download',action='store_true')
    parser.add_argument('--rerun',action='store_true')
    a=parser.parse_args();policy=load(a.policy);base=policy['baseline']
    source=a.baseline.resolve();framework=source/'out/framework'
    downloads=a.output.resolve().parent/'downloads'
    if a.download:
        for kind, target in [('framework',framework),('emulator',source/'out/verified-emulator')]:
            z=downloads/(kind+'.zip')
            download_artifact(base[kind+'_artifact'],base,z)
            extract(z,target)
    for kind,target in [('framework',framework),('emulator',source/'out/verified-emulator')]:
        z=downloads/(kind+'.zip')
        need(sha(z)==base[kind+'_artifact']['sha256'],'archive digest mismatch')
        print('EXTRACTED_ARCHIVE_VERIFIED',kind,verify_extracted(z,target),flush=True)
    emulator=source/'out/verified-emulator/qemu-arm'
    need(sha(emulator)==base['emulator_sha256'],'emulator hash mismatch')
    os.environ['PATH']=str(emulator.parent)+os.pathsep+os.environ['PATH']
    os.environ['PYTHONDONTWRITEBYTECODE']='1'
    os.environ['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1'
    os.environ['UBSAN_OPTIONS']='halt_on_error=1'
    receipt=verify_bundle(source,framework,base)
    if a.rerun:
        receipt['independent_rerun']=rerun_original(source,framework)
    else:
        raise EvidenceError('acceptance requires original-binary reruns')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(receipt,ensure_ascii=False,sort_keys=True,indent=2)+'\n')
    print('LANGUAGE_EVIDENCE_VERIFIED '+json.dumps({k:v for k,v in receipt.items() if k not in ('basis','packages','artifacts','font_receipt')},sort_keys=True))
    print('LANGUAGE_PACKAGE_BINDINGS '+json.dumps({n:{'sha256':p['sha256'],'capabilities':p['targets'][0]['capabilities']} for n,p in receipt['packages'].items()},sort_keys=True))

if __name__=='__main__':
    try:
        main()
    except Exception as exc:
        if isinstance(exc,(KeyError,FileNotFoundError)):
            import traceback
            traceback.print_exc()
        print('LANGUAGE_EVIDENCE_FAILED',type(exc).__name__,str(exc) if isinstance(exc,(EvidenceError,FileNotFoundError,KeyError)) else 'see preceding build/test log',file=sys.stderr)
        sys.exit(1)
