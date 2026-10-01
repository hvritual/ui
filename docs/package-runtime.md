# Packaged applications in the native runtime

This is the next #56/R3 slice after the container codec: the actual production
`ui-framework` loads a `.pui` instead of reopening a trusted application directory.
It is **development package admission**, not publisher authentication, public SDK
certification, production sandbox, performance certification or board approval.

## Invocation and compatibility

```sh
# Cloud/desktop functional run, using the unchanged executable for either app:
./ui-framework --headless --profile imx6ul-1024x600 \
  --package packages/coffee.pui --allow-unsigned-package \
  --seconds 1 --output NEWDIR
./ui-framework --headless --profile imx6ul-1024x800 \
  --package packages/control-panel.pui --allow-unsigned-package \
  --seconds 1 --output OTHER_NEWDIR
```

Exactly one of `--asset-root` and `--package` is required. The old trusted-directory
mode remains an explicit compatibility path; a rejected package never silently
falls back to it. `--allow-unsigned-package` without a package is an error.
Unrecognized authentication schemes still reject even with this switch. SHA256
checks integrity, not who published the bytes. P7 owns signing/key distribution,
anti-rollback and interrupted deployment recovery.

Physical startup remains an explicit `--physical` operation on an isolated board
with display/input ownership. The 800 target still needs its independently verified
touch configuration. This slice performs no physical run and does not stop existing
services. Synthetic replay remains headless-only; physical replay is rejected before
device access. Existing `run-framework.sh` defaults to the Coffee directory; it is
not silently changed into a package install or unsigned-production launcher.

## One verified immutable input

`pui_loaded_open` opens a regular file with no-follow/nonblocking flags, bounds its
size, reads it once into a private anonymous mapping and checks length/file metadata
before closing the descriptor. It seals the mapping read-only **before** the existing
C parser validates the container. It does not use a mutable file-backed mapping or
extract members into a temporary directory. The source pathname is never reopened
for application/catalog/atlas/image loading.

The Framework retains the package through the engine/application lifetime. Source
and atlas consumers make their own bounded copies when their existing APIs require
ownership; they copy the admitted bytes, not another filesystem read. The loader's
reference handles are single-owner-thread objects and cannot be copied by C struct
assignment. Releasing the caller handle does not invalidate the running Framework.
Normal and failed-start cleanup dispose resources and release the package.

Tests truncate/unlink the original file after admission, release the caller handle,
then render and interact on both viewports. A test-only child deliberately attempts
a write and must fault, proving mapping protection. It is absent from the production
ELF. A bounded number of EINTR retries prevents signal-only read loops; regular-file
I/O has **no claimed hard wall-clock deadline**, especially on remote filesystems.

## Runtime policy, not manifest-only checks

The codec's independent package format, runtime API and SDK API fields are checked
against the actual host constants. Version 1 is an internal contract, not an assertion
that a complete public SDK 1.0 exists. Target profiles are checked against the chosen
software viewport, not treated as physical controller certification.

- `ui.core` permits the existing bounded framework command set.
- `ui.keyboard.ascii` permits native keyboard operations/keyboard overlays.
- `ui.images` permits image components, image resources, image delegates and
  reactive resource bindings. These checks happen before native mutation; a
  subsequent scene check also rejects undeclared image references.

Unknown capabilities and unsupported privileged commands reject. Catching a script
exception cannot clear the native failure latch. Keyboard authorization does not
expose Password/PIN values; the existing native text owner and screenshot suppression
remain unchanged. No arbitrary OS/shell/network/native plugin binding is added.

The requested `heap_bytes` is passed to the real QuickJS application VM memory limit
before boot. It means **that VM's managed heap**, not total process RSS, the renderer
VM, native component memory or decoded texture caches. Tests run the same additional
2 MiB allocation with 1 MiB and 8 MiB budgets: the first fails, the second succeeds.
`asset_bytes` bounds stored member bytes, not expanded runtime image memory. Existing
component/resource size and count limits still apply; total UI+IME/process budgets
belong to later measured gates. This is not a general untrusted-code sandbox.

An explicitly configured external `--media-store` retains the existing P4 signed
update/rollback path and requires `ui.images`. Those later updates are a separate
resource authority, not falsely described as immutable members of the original
`.pui`. With no media store, package resources need no application directory.

## Acceptance and evidence

```sh
make check check-targets check-runtime-boundary test-runtime-boundary
make test-framework test-framework-arm test-framework-sanitize verify-framework
make build-board-framework test-board-framework-package test-board-framework-verifier
# Focused integration reruns after the ordinary dependencies/assets are built:
make test-package-runtime test-package-runtime-arm test-package-runtime-sanitize
```

The canonical Framework gates run the package tests too; a codec-only success cannot
satisfy integration. Original frozen Coffee assertions run through a test-only package
open adapter, preserving all 42 frames and six replays (navigation, media, dragging,
keyboard and privacy). The ordinary directory/frozen-C comparisons remain intact.

The actual production Native and static ARM ELFs each run both Coffee and a distinct
counter/settings/modal package. Within an architecture the ELF SHA is identical;
Native and ARM hashes are naturally different. Changing only the counter application
inside a rebuilt package must change behavior/pixels without relinking Runtime.

Negatives include default unsigned denial, tamper before framebuffer access, incompatible
version/target/budget/capability/authentication, malformed catalog with valid hashes,
image access through alternate commands, real-keyboard authorization, heap exhaustion,
metadata-only error reporting, output overwrite refusal and missing/changed files.
Native/ARM/sanitized package-framework and lifetime outputs must match. Sanitizer builds
instrument project C, changed host/loader code and Unicode; they do not retroactively
instrument prebuilt PocketJS/QuickJS libraries. The separate program-contract workflow
retains its fully instrumented QuickJS/program checks.

`package-runtime-verification.json` and `static-package-proof.json` bind source, ELF,
package hashes, raw logs and pixels. Deployment includes the **same tested** Coffee and
panel `.pui` files and `PACKAGES.md`. Reports retain `authenticated=false`,
`physical_hardware=false` and `production_admission=false`; `pui_admission` in the legacy
board manifest remains false for production, while `pui_development_loader=true` records
this bounded software path. #56 remains open until the R3 review/acceptance is complete;
#12/#49 are not auto-approved.

## Optional offline input capability

`ui.ime.pinyin` (bit 8) is an opt-in additive capability requiring ASCII keyboard
capability and the full admitted dictionary/font/NOTICE member set. Legacy
packages retain their original behavior and do not acquire IME permission merely
because the Runtime links a decoder. See [input-keyboard.md](input-keyboard.md)
for the exact rendered repertoire, resource budgets and separate acceptance.
