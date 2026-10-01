# Visible offline input on the fixed Runtime

This is the rendered R4/#12 integration of the accepted offline provider. It is
not R5 product-language admission, physical board acceptance, a performance
claim, a public SDK or authenticated package distribution.

## Explicit opt-in; old applications remain unchanged

The existing Coffee and control-panel packages retain their original capabilities
and keyboard. A third reference package, `coffee-ime.pui`, requests
`ui.ime.pinyin` in addition to core, ASCII keyboard and images. It contains the
same Coffee application and a complete input resource set: `input.atlas`,
`pinyin.dat`, and `PINYIN-NOTICE.txt`. Missing resources, an IME capability without
ASCII keyboard capability, or input resources without the capability fail closed.
The dictionary identity must match the locked real AOSP dictionary. Runtime
ownership, unsigned-deny default and the existing managed application-heap limit
remain in force. A hash is not publisher authentication.

A package-authorized application receives `inputLocales=3` in its start
configuration. It can use `keyboard.languages(active_mask, initial_locale)` while
configuring the native editor and field mode 4 for the admitted text repertoire.
Mask/locale 1 is English; 2 is Simplified Chinese. The default/legacy public
contract remains separately identified in `contracts/text-input.json`; the
optional software path is `contracts/offline-input.json`. Private native glyph
references are never accepted from application commands.

## Keyboard and interaction

The weighted QWERTY, symbol and digit rows and explicit language selection popup
are independently authored, using Qt Virtual Keyboard's layout/language
interaction ideas. No Qt/QML frontend, runtime or style assets are copied or
linked. The separately licensed, locked AOSP PinyinIME dependency remains as
recorded in `docs/offline-pinyin.md`.

The existing F6/Component/Overlay/TextSession owners are reused. Button presses
queue semantic actions after dispatch; callbacks never mutate a borrowed UI tree.
Held contacts bind field/session/focus/engine/revision, layout generation and
candidate request identity. Language, field, page and lifecycle changes cannot
replay an old key or select a new candidate accidentally. The popup background
intercepts taps, including a background form-confirm button; it dismisses the
popup without submitting the form.

The first text field supports printable ASCII and individually admitted BMP Han
characters. Name values are at most 64 graphemes and 256 UTF-8 bytes. Chinese
preedit is separate from committed text; five candidates are visible per page,
with partial choices and paging. Holding a clipped candidate previews its full
bounded text in the preedit strip; moving/cancelling does not commit. A candidate
Enter commits once, never simultaneously confirms the form. Backspace deletes
committed graphemes or the current composition according to the existing adapter.
Switching language, hiding or leaving the field cancels preedit without changing
committed text. Number/PIN remain ASCII digits only; Password remains printable
ASCII. Password/PIN have no language chooser, dictionary, learning or history.

The current input locale is on the space key and in the language popup. Display
locale, input locale and layout mode are independent. Only English and Simplified
Chinese are selectable in this slice. Locale display names currently use English;
this is not a claim that other regional keyboards or UI translations are done.

Both viewports use separately checked geometry. On the 600-height screen the
field, editing controls, preedit, candidates and keyboard do not cover one another.
The 800-height profile leaves additional separation rather than shrinking keys.

## Glyph and immutable resource boundary

The input atlas uses the full locked dictionary `valid_utf16.txt` inventory plus
printable ASCII: 16,561 entries, not a test-word subset. The source font is the
existing hash-locked Noto font used by the project; source font files are not
shipped. Runtime validates the exact cmap inventory and 24x30 coverage cells in
slot 1. The atlas is bounded by 13 MiB, and the complete application package
remains below the existing 16 MiB container limit. This admits only simple BMP Han
and ASCII; it is not arbitrary Unicode shaping, BiDi, accents, emoji, Hangul or
Japanese input. Unsupported field values reject instead of silently rendering
missing glyphs. The underlying Unicode editor has a broader buffer capability
than this particular rendered widget.

The unchanged upstream resource event is limited to 2 MiB. Native validates the
whole immutable atlas, then sends private 1 MiB maximum contiguous PIF1 segments.
The guest rejects missing initial segments, reordered/duplicate segments, length
mismatches and totals beyond 13 MiB. Only the complete atlas reaches the actual
PocketJS font loader. No upstream event limit, renderer revision or frame clock
is changed. Temporary staging is released after loading. Font/texture/process RSS
is outside the application JS-heap limit and still needs the P5 total-resource
budget; the atlas size must not be mistaken for the whole process footprint.

The Framework retains the verified read-only `.pui` snapshot. Input continues
after the caller releases its handle and the original file is truncated/deleted.
The provider checks/copies the fixed dictionary before owning a child; normal
startup remains single-threaded before fork. It permits one in-flight query and
one newest queued query. The 2,000 ms recovery deadline and cancellation/cleanup
are inherited from the accepted core; no cloud lookup or first-use download is
introduced. This is not a hardened untrusted-code sandbox. Resource corruption
rejects startup; a later query failure is shown as unavailable, with an explicit
language switch used to retry, not a silent fallback reported as Chinese success.

## Privacy and diagnostics

Password/PIN values never return to the application. Production screenshot APIs
refuse every open editor and residual editor pixels until a safe frame is
presented. Reports add only `ime_commits` and `ime_candidate_batches`, maintained
by the native bridge, never text/key/candidate content or their hashes.

`--replay-realtime` is an explicit **headless-only diagnostic**, requiring
`--replay-input`. It paces the already bounded synthetic input trace so the real
asynchronous child can execute. It preserves intervening logical ticks and does
not change the physical UI loop. It is not a display-latency or physical-input
measurement. Replays/screenshots in acceptance use synthetic values only.

## Build and checks

Prepare the exact dependency checkout at `out/ime/checkout`, using the commit in
`toolchains/pinyin.lock.json`, then run `python3 scripts/ime.py prepare`. The
canonical Coffee workflow performs this checkout with read-only permissions and
installs the existing ARM toolchain's C++ driver. Production code uses no Qt.

```sh
make check check-targets test-runtime-boundary verify-language-packs
make test-framework test-framework-arm test-framework-sanitize verify-framework
make build-board-framework test-board-framework-package test-board-framework-verifier
# Focused slice; it also runs as part of the canonical Framework tests:
make test-input-view test-input-view-arm test-input-view-sanitize
```

The old 42 images/six replay assertions remain. New real-Core input tests cover
both target sizes, real candidate/partial selection/paging/commit, native glyph
pixel blending against an independent oracle, English/Chinese switching, popup
interception, field/overlay ownership, digits, sensitive fields, cancellation,
resource lifetime and repeated provider/editor lifecycles. A deliberately wrong
glyph expectation must fail. Native/ARM/repeat/sanitizer images must match.

The production Native and static ARM ELFs each execute the exact input package,
including actual paced candidate commit and screenshot suppression. Correctly
hashed but wrong dictionary/atlas data, capability violations, private glyph refs,
unsupported input characters and malformed diagnostic options are rejected.
Existing two-application package proofs run with these same Runtime binaries.
The new `input-verification.json` and static `input-proof.json` bind source,
resource receipts, package, executable, images and raw logs. ARM/QEMU is functional
software evidence only. Provider full-engine sanitizer is separate from complete
Framework project-code sanitizer; prebuilt upstream Rust/QuickJS dependencies are
not described as retroactively instrumented.

A local `--debug-font` option exists only for clearly marked development images.
The acceptance verifier rejects such font receipts. It must not approve debug
images as the final pinned-font output or package them as a release artifact.

## Isolated board diagnostic after software acceptance

```sh
./run-input.sh imx6ul-1024x600 180
```

This explicit development entry selects the IME package with unsigned-development
opt-in. It reuses the existing board runner, display/input ownership restrictions,
known-controller discovery and independent 800 calibration requirements. It does
not stop machine control/network/OTA or alter BSP/display modes. Test only with
synthetic values; do not record real credentials. Physical approval remains
pending until the actual target's original reports and LCD/touch evidence exist.
