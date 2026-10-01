# Application package container — first R3 slice

Owner: #56; later public contract hand-off: #40. Base is the accepted R2 merge
`ca7e1fb4d887f72a6f849f0e1a4888a0b7349893` (PR #62).

This slice implements a deterministic `.pui` builder, an independent Python
verifier and the actual dependency-free C container parser. **It does not yet
connect `.pui` to the production `ui-framework` startup path.** R2 trusted
application directories still run exactly as before. R3 is not complete.

## Contract and scope

`contracts/application-package.schema.json` describes the human/AI-readable
build manifest; `application-package.example.json` is a usable example. The
builder rejects duplicate keys, unknown fields, float/bool integer substitutes,
unsupported file names, symbolic links and output overwrite. No application is
executed. No source font, private adapter, native library or signing key is put
in the package. Member file names are a deliberately small v1 allowlist matching
R2 directories, not a general archive/filesystem format.

Runtime API level, SDK API version and container format version are separate
fields, even though this experimental codec currently tests each with value 1.
This does not announce a public SDK 1.0 or certify a corresponding Runtime load
API. A version range is inclusive; unknown formats, unsupported SDK versions,
unavailable capabilities and the wrong target fail closed. Declared targets are
not hardware admission. Capability names denote requested grants; they are not
proof that an application confines itself to those operations.

The native `pui_validate` accepts an immutable buffer and a caller-supplied policy.
It allocates no heap, extracts no files, evaluates no code and makes no system
calls. It returns bounded borrowed member views only after the complete table
and every digest pass; on failure the output is zeroed. Caller buffer and output
must not overlap. The caller must retain the buffer unchanged for the full view
lifetime. `probe.c` is test-only, not a replacement application Runtime.

Unsigned packages are rejected by default. Explicit `allow_unsigned` permits
container verification for controlled development only. SHA256 binds the actual
header/table/payload bytes for corruption detection; a publisher can recompute
it, so **integrity is not authentication**. No signing, key storage, revocation,
rollback protection, untrusted-code sandbox or production authorization is claimed.
Future signed formats must not be silently treated as unsigned v1 containers.

## Experimental v1 wire

All numeric fields are little endian; no C struct is deserialized from input.
The fixed header is 192 bytes. The table has at most 16 records of 96 bytes,
followed by contiguous, uncompressed member bytes. The whole file is <=16 MiB.
No compression, nesting, directories, overlaps, gaps or trailing bytes exist.

| Offset | Encoding | Meaning |
|---:|---|---|
| 0 | 8 bytes | `PUI1\r\n\x1a\n` magic |
| 8 / 12 | u32 / u32 | Format 1 / header size 192 |
| 16 / 20 / 24 | u32 each | Runtime API min / max / SDK API |
| 28 / 32 / 36 | u32 each | Target mask / capability mask / file count |
| 40 | u64 | Total member bytes |
| 48 / 52 | u32 each | Requested application heap / member-byte budget |
| 56 / 60 | u32 each | Fixed source ceiling 262144 / authentication scheme 0 |
| 64 / 128 | 64 / 32 bytes | App ID / version, ASCII, NUL terminated and zero padded |
| 160 | 32 bytes | SHA256(header[0:160] + table + payload) |

Each record is `name[48] + offset:u64 + length:u64 + sha256[32]`. Names are
zero-padded and strictly increasing, offsets contiguous, and lengths nonzero.
Required: `application.js`, `catalog.json`, `labels.atlas`, `builtin.rgba`.
Optional: `alternate.rgba`, `Noto-LICENSE.txt`, `IMAGE-LICENSE.txt`.
Payload validation for glyph/image formats remains the existing Runtime's job;
container validation alone does not make malformed application content runnable.

Target bits: 600=1, 800=2. Capability bits: ui.core=1 (required),
ui.keyboard.ascii=2, ui.images=4. All other bits fail closed.
Application source <=256 KiB; catalog <=64 KiB; requested heap 1..8 MiB.
`asset_bytes` counts ALL members, not just pictures. The heap field describes
an application VM budget, not total process RSS or an accepted P5 memory budget.
This slice compares requests with policy limits; the future execution host must
actually apply heap limits, operation grants and aggregate resource constraints.

## Usage and tests

```sh
python3 scripts/pui.py build \
  --manifest contracts/application-package.example.json \
  --assets out/framework/assets --output coffee.pui
python3 scripts/pui.py verify --input coffee.pui \
  --target imx6ul-1024x600 --allow-unsigned
make -f scripts/package.mk test-package-contract
make -f scripts/package.mk test-package-contract-arm
make -f scripts/package.mk test-package-contract-sanitize
```

Tests compile and invoke the actual C parser on Native, static ARM and
ASan/UBSan, using independently produced binary fixtures. Digest vectors,
truncation, arithmetic limits, unknown versions/targets/capabilities, default
unsigned denial, invalid tables/names/padding, file hashes, strict manifest
parsing, symlinks and overwrite refusal are exercised. Native additionally
compares 2000 deterministic byte mutations against Python. These are synthetic
container tests, not same-ELF application or physical-board acceptance.

Reports bind source, compiler, binary and raw evidence hashes and explicitly say
`native_runtime_integration=false`, `executed_application=false`,
`physical_hardware=false`, `authenticated=false`. Existing 19 workflows, source
locks, production builder, renderer, keyboard, input, and device services are
unchanged. This new parser is intentionally not yet an admitted production link
input; that must be an explicit ownership change during the next slice.

## Remaining R3 exit

1. Add a bounded production `--package` startup route, without interpreting or
   extracting unverified bytes; keep any directory diagnostic mode explicit.
2. Bind target/API/capability policy and requested heap to the real application
   host. Verify grants at command dispatch, not only in the package manifest.
3. Preserve one verified immutable payload snapshot through resource use and
   clean up resources on rejection/cancel/exit; no verify-then-reopen race.
4. Run both actual R2 applications from packages with identical ELF SHA, retain
   the 42 baseline frames/six replays, keyboard privacy and dual-viewport gates.
5. Verify new artifacts before claiming R3 accepted. #56, #40 and physical/IME
   issues remain open. Signing/deployment/rollback production work remains P7.
