# Offline Pinyin input — provider and session boundary

This R4 / #12 slice connects a genuine offline dictionary engine to the existing
native TextSession. It also supplies a Qt-inspired keyboard geometry and language
selection model. It does **not** yet replace the rendered ASCII keyboard, add
candidate glyph rendering, grant a package IME capability or admit a product locale.
Do not use successful Chinese text-buffer tests as evidence of a visible Chinese UI.

## Source/reference decisions

The user asked to use Qt/QML Virtual Keyboard as a reference, not to introduce Qt.
The interaction references are:

- Qt `ChangeLanguageKey`: a language-popup mode or a next-language toggle. We
  choose an explicit popup. The selected input language is available to the space
  key label; changing input language does not change the Coffee display locale.
- Qt `KeyboardLayout` / `KeyboardRow`: row-relative key weights and distinct
  letter/symbol/digit layouts. Our geometry implementation is independently
  authored C; it does not copy QML, Qt frontend source, icons or styles.
- Qt `VirtualKeyboardSettings`: available languages versus the active subset.
  The initial layout model has English and Simplified-Chinese Pinyin only.
  Password/PIN stay direct and cannot enable an IME or a language popup.

Official references (reviewed 2026-10-01):
https://doc.qt.io/qt-6/qml-qtquick-virtualkeyboard-components-changelanguagekey.html
https://doc.qt.io/qt-6/qml-qtquick-virtualkeyboard-settings-virtualkeyboardsettings.html
https://doc.qt.io/qt-6/qtvirtualkeyboard-overview.html
https://doc.qt.io/qt-6/qtvirtualkeyboard-user-guide.html

The conversion engine is AOSP PinyinIME, as maintained in the separately licensed
third-party subtree of Qt Virtual Keyboard v6.8.3. The choice is a reproducible
feasibility baseline, not a claim that this is the latest Qt or the best engine
for every language. `toolchains/pinyin.lock.json` pins commit, all 49 original
file hashes, prepared hashes, the real dictionary and the local patch.
Qt's attribution identifies this subtree as Apache-2.0:
https://doc.qt.io/qt-6.8/qtvirtualkeyboard-attribution-pinyin.html
Preserve its NOTICE and attribution with any engine/dictionary distribution.
Qt's surrounding QML/module license is distinct and its frontend is not included.

## Standalone adaptation and the initial failed build

Initial PR #65 head `36ca21fa1384318f60e04f06a7644b46995ebfa9`, workflow
`36809131322`, failed because the maintained subtree still used `QFile`. That
failed probe remains evidence; the upstream CMake also linked Qt Core.

Preparation reverses exactly the subtree's `0005-Fix-string-cast.patch` and
`0004-Bundle-pinyin-dictionary-in-the-plugin.patch`, restoring its FILE-based
implementation. It then applies `patches/pinyin-no-user-dictionary.patch` so a
null user-dictionary argument really disables opening/loading it. This is file
I/O and lifecycle adaptation, not a new Pinyin conversion algorithm or a fake
Qt API shim. Original and prepared files are independently hash checked.

No user dictionary, learning, prediction history, network, cloud candidates,
initial runtime dictionary download or desktop input-method daemon is enabled.
The dictionary is exactly 1,068,442 bytes; the engine refuses any other digest.

## Ownership and process boundary

`pinyin.h` is a single-owner Linux API. Open/close must run on the one UI owner
in a single-threaded caller; this implementation is not safe for arbitrary
multi-threaded fork users. A future runtime integration must enforce that
precondition or use an exec-based worker instead. There are no UI bindings yet.

Opening copies the caller's dictionary to an anonymous sealed file, verifies the
copied bytes, and passes that descriptor to an isolated child. The child closes
inherited file descriptors except the dictionary and its private sequenced-packet
socket. Its core dumps are disabled and it dies when the parent disappears.
The UI owner never performs dictionary search. No decoder OS/network/actuator
API is exposed to an application. This process isolation is not a production
sandbox and does not certify a whole-machine security boundary.

At most one request is in flight and one newest request waits; obsolete queued
spelling is erased. Requests carry field/session/focus/engine/revision and a
separate request sequence. Page changes also replace the request sequence.
The parent performs nonblocking bounded socket operations and applies a 2-second
startup/request cancellation deadline. The native decoder is killed and reaped
on timeout or close. The limit is a failure-recovery ceiling, not a measured
keyboard latency target or a promise of constant-time process teardown.

Current bounds: 32 lowercase spelling bytes with explicit apostrophe separators,
16 Han codepoints per returned candidate, five candidates/page, at most 1,280
candidate indices. This dictionary adapter admits BMP Han U+3400..U+9FFF and
rejects unsupported candidate encoding. It is not an arbitrary-Unicode renderer.
The owned worker releases its heap at process exit; the IME worker's total
RSS/PSS, dictionary pages and font cache belong in P5's later resource budget.

## Existing TextSession, not a second focus manager

`ime/session.h` borrows the existing focused TextSession. F6 remains responsible
for actual field/component focus and destruction. The caller drives `step` after
input dispatch and passes the latest token to every command. A changed owner or
focus generation discards old candidates instead of writing another field.

Spelling updates preedit only. A full candidate commits once. Selecting a partial
candidate fixes its prefix, preserves the unconsumed spelling and asks the real
engine for the suffix; it must not discard trailing letters. Pinyin Enter chooses
a candidate, never also submits a form. Only a separate non-composing Enter may
return the SUBMIT effect. English insertion and committed-text deletion reuse
the existing grapheme implementation. No normalization or trimming is added.

Changing input language cancels preedit and invalidates pending results, but
preserves committed text. Missing/corrupt dictionaries keep the previous language
and return an explicit error. An engine failure never silently declares Chinese
input successful. Recovery is explicit English → Chinese reactivation. Backspace
edits the spelling; with a fixed prefix and no remaining spelling, cancellation
clears the composition. Rich editor undo and prediction are not provided.

The controller owns no logger. Snapshots expose state/counts only. Candidate text
is obtainable only through the explicit rendering accessor with a matching live
session; production tracing must never use that accessor. Password/PIN and
read-only/disabled sessions cannot acquire this IME. The existing application's
screenshot suppression remains unchanged and is not weakened by this slice.

## Layout model and integration contract

The model reserves an editor, preedit, candidate row and four weighted key rows
for each actual 1024×600 / 1024×800 software profile. It checks 48-pixel minimum
key geometry and does not require the 600 screen to reuse 800 coordinates.
Alphabet, symbols and digits are separate modes; digits have no Shift/language
key. A language popup and mode change advance a generation so a view can drop
held-key releases from the previous layout. The view must bind these roles to
existing F6 / overlay / keyboard components, not add its own hit-testing tree.

This geometry is not a screenshot, rendered view or touch usability acceptance.
No fake candidate view has been added. `assets/locales/input-capabilities.json`
retains its existing product admission flags until genuine rendering/integration
and independent physical evidence are available.

## Verification

```sh
# CI checks out the pinned original subtree into out/ime/checkout first.
python3 scripts/ime.py prepare
python3 scripts/ime.py test --mode native
python3 scripts/ime.py test --mode arm
python3 scripts/ime.py test --mode sanitize
python3 scripts/ime.py verify
```

The tests execute the actual decoder/dictionary plus unchanged Unicode and text
core. They cover known nonconstant candidates, partial choice, candidate pages,
rapid newest-only requests, stale tokens, language/field changes, committed-text
limits, explicit missing/corrupt dictionaries, timeout/restart, sensitive policy,
20 worker lifecycles and 12 layout/target/language combinations. Positive/repeat
outputs must match across Native and static ARM. An intentional assertion must
fail. The sanitizer mode rebuilds engine, provider, controller and Unicode with
ASan/UBSan; it does not claim that the existing UI renderer is instrumented.

Remaining R4 integration: actual F6 keyboard view and language popup, dynamic
CJK glyph/candidate/text rendering rather than the static-label atlas, provider
lifetime within the runtime, authorized package resources/capability, same-ELF
real Coffee package scenarios and dual-target visual tests. R5 records per-locale
software/physical evidence afterward. Real boards, SDK, signing and performance
admission are independent.
