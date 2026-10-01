# Language capability, evidence and release claims

R5 / #67 consumes #12's implemented language facts; it does not add another
keyboard, engine, focus tree or Runtime. The authority for capability claims is
`assets/locales/input-capabilities.json`, with the optional implementation
contract in `contracts/offline-input.json`. `contracts/language-release.json`
contains only release scope, immutable evidence anchors and follow-up routing.
It is not a duplicate implementation-status database.

## Scope

This software release candidate covers restricted en-US (printable ASCII) and
zh-CN (opt-in offline Pinyin and the pinned BMP Han repertoire). The remaining
nine locale rows remain present and not supported in this release. First-market
confirmation is pending. Software verification does not set
`product_input_admitted`, hardware or production admission to true. #12 remains
open for its actual remaining work; R5 completion is narrower than all of #12.

Static UI catalogs are distinct from editable text. A German/French/Spanish/
Portuguese catalog does not prove a regional keyboard. `not-required` for direct
Latin IME is not a missing implementation, but neither proves the keyboard works.
Unicode buffering is not shaping or a rendered editor. Password/PIN never acquire
composition or a language chooser. English and Chinese input do not imply accents,
Hangul, kana, emoji, bidirectional editing or arbitrary Unicode.

## Real gates and generated artifacts

From the root:

```sh
make -f scripts/language.mk check-language-admission test-language-admission
# After the canonical complete Framework/ARM/sanitizer/package workload:
make -f scripts/language.mk verify-language-admission
```

The canonical Coffee workflow runs the new gate after its original tests and
static package gates. It verifies the current commit's raw reports, binaries,
packages, font receipt, dictionary and both target/mode images. `admission.json`
and `support.md` are derived from the same in-memory result under
`out/framework/language-admission/`. Do not maintain a second hand-edited support
table. The report includes per-locale display, keyboard, editing, IME, text layout,
600/800 software and physical states, restrictions, privacy, package hashes,
resources, source provenance and follow-up issues.

`check` alone deliberately reports no software acceptance. Missing artifacts,
source drift, wrong package/font, debug fonts, missing target/mode results,
changed raw bytes, false physical/product flags and broadened locale declarations
fail closed. Guard unit fixtures are not evidence of working Runtime behavior.

## Independent baseline review

The Language Admission Evidence workflow downloads the *existing* R4 artifact by
numeric ID and SHA256, verifies its source commit/tree against a separate checkout,
and reruns the ORIGINAL Native and static ARM production input proofs. It also
reruns original Native view and sanitizer executables against the exact pinned
font/package, including intentional-failure cases. It neither recompiles the
Runtime nor replaces its renderer, dictionary or golden expectations. QEMU is
software functional evidence, never a board latency measurement.

The review receipt explicitly distinguishes its checker commit from the Runtime
build commit. Reuse of historical evidence is permitted only when all corresponding
tracked sources are byte-identical; R5's only existing-file change is an additive
Coffee-workflow verification step. Ordinary future Runtime PRs instead get fresh
language verification inside the canonical Coffee workflow. Historical review
anchors are not a permanent substitute for new builds.

GitHub artifact retention is finite. A missing/expired review artifact must fail;
refresh the reviewed evidence anchor from a new accepted build in an explicit PR,
or preserve the original digest-bound archive in approved durable storage. Do not
silently resolve `latest`, accept a path-only reference or relabel another commit.
The dedicated historical-review workflow is path-scoped to admission code/policy;
ordinary Runtime changes use fresh current-build evidence and do not depend on
that historical download remaining online forever.

No font source, dictionary, application package or signing material is copied into
the small language-review deliverable. It contains reports, source of the checker,
hashes and synthetic test logs only. CI code/permissions and evidence still need
review: this checker is not an oracle against someone changing the checker itself.

## Follow-up ownership and frozen scenarios

- #68: de-DE/fr-FR/es-ES/pt-PT regional layouts/accents, separate per-locale PRs.
- #69: zh-TW market-specific input scheme and resources; #70: ja-JP conversion;
  #71: ko-KR composition; #72: ar shaping/RTL; #73: th-TH complex text.
- #49 / R6: independent 600/800 probes, actual keyboard/candidate/locale-popup
  operation, screenshot/touch human review, disconnect/SYN_DROPPED recovery,
  damaged/missing resource and restart behavior. Synthetic values only.
- #7 / P5: cold/warm keyboard, continuous English/Pinyin, candidate paging/partial
  selection, long delete, language/field switching and progress-animation
  concurrency. Freeze Runtime/package/engine/dictionary/font hashes. Count total
  UI plus IME/cache resources, not only the managed JS heap. Real samples and
  visible feedback are required; replay pacing is not a performance result.
- #9 / P7 and #10 / P8: bad/missing language resources, update interruption,
  rollback, child timeout/restart, repeated open/close, long soak and device-service
  isolation. Keep publisher authentication and production flags false until their
  own gates pass.

Only adding future language adapters and evidence can change the release scope;
editing a flag is never enough. No new performance optimization or device control
is part of this work.
