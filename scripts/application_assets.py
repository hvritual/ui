"""Build independent application directories and the fixed private adapter.

This is an internal, trusted R2 payload format, not a signed .pui package loader.
No application's source or catalog contributes to the generated native header.
"""
from __future__ import annotations
import hashlib
import json
import shutil
from pathlib import Path


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(',', ':')) + '\n')


def build_assets(root: Path, out: Path) -> None:
    src = root / 'out/coffee/assets'
    manifest = root / 'out/coffee/assets.json'
    if json.loads(manifest.read_text())['font_source']['debug_only']:
        raise RuntimeError('debug font rejected')
    resources = ('labels.atlas', 'builtin.rgba', 'alternate.rgba', 'Noto-LICENSE.txt', 'IMAGE-LICENSE.txt')
    applications = {'coffee': out / 'assets', 'control-panel': out / 'applications/control-panel'}
    for directory in applications.values():
        directory.mkdir(parents=True, exist_ok=True)
        for name in resources:
            shutil.copy2(src / name, directory / name)
        (directory / 'framework.js').unlink(missing_ok=True)
    locales = json.loads((root / 'apps/coffee-demo/locales.json').read_text())
    if list(locales) != ['zh-CN', 'en-US', 'de-DE', 'fr-FR', 'es-ES', 'pt-PT']:
        raise RuntimeError('locale ordering changed')
    keys = ['title','subtitle','demo','next','back','cancel','start','confirm','making','done','home','media']
    keyboard = json.loads((root / 'assets/locales/keyboard-ascii.json').read_text())
    catalogs = []
    for locale, labels in locales.items():
        text = {i + 1: labels[key] for i, key in enumerate(keys)}
        text.update({13: locale, 14: 'Theme'})
        text.update({100 + i: name for i, name in enumerate(labels['names'])})
        text.update({keyboard['label_ref_base'] + i: name for i, name in enumerate(keyboard['labels'])})
        text.update({keyboard['ascii_ref_base'] + c: chr(c) for c in range(32, 127)})
        catalogs.append(text)
    write_json(applications['coffee'] / 'catalog.json', catalogs)
    shutil.copy2(root / 'apps/coffee-framework/application.js', applications['coffee'] / 'application.js')
    # This independent reference application uses only locale index zero.
    # Repeating its catalog satisfies the current private six-slot renderer wire;
    # it does not certify six product languages.
    panel = json.loads((root / 'apps/control-panel/labels.json').read_text())
    panel.update({str(5200 + i): str(i) for i in range(100)})
    write_json(applications['control-panel'] / 'catalog.json', [panel for _ in range(6)])
    shutil.copy2(root / 'apps/control-panel/application.js', applications['control-panel'] / 'application.js')
    adapter = (root / 'hosts/linux/engine/scene_guest.js').read_bytes()
    if not adapter or b'\x00' in adapter or len(adapter) > 256 * 1024:
        raise RuntimeError('private adapter source rejected')
    include = out / 'include'
    include.mkdir(parents=True, exist_ok=True)
    rows = [','.join(str(b) for b in adapter[i:i + 24]) + ',' for i in range(0, len(adapter), 24)]
    (include / 'private_scene_guest.generated.h').write_text(
        '/* Generated only from the fixed platform adapter; no app data. */\n'
        'static const unsigned char pocket_scene_guest[] = {\n' + '\n'.join(rows) + '\n0};\n')
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    app_hashes = {key: {p.name: digest(p) for p in sorted(directory.iterdir()) if p.is_file()}
                  for key, directory in applications.items()}
    write_json(out / 'assets.json', {
        'p4_assets_manifest': digest(manifest), 'files': app_hashes['coffee'],
        'applications': app_hashes, 'private_adapter_source_sha256': digest(root / 'hosts/linux/engine/scene_guest.js'),
        'private_adapter_header_sha256': digest(include / 'private_scene_guest.generated.h')})
