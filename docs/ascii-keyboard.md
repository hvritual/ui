# Visible ASCII fields and touch keyboard (#12)

This is the second P4A functional slice after the native TextSession core.
It is real Component/F6/TextSession/Engine integration, not a screenshot-only
prototype, and it performs no new performance optimization. Keep #12 open for
actual offline IME, other layouts and per-target field acceptance; #49 retains
its independent physical gates.

## Entry and outcome

The Coffee Home footer now has **Text input**. It opens a local four-field demo:
Name (printable ASCII, 64 chars), Number (0-9 integer digits, 16 chars), Password
(printable ASCII, 64 chars) and PIN (0-9, 8 chars). Number is not a floating-point,
locale-formatted, signed or decimal field. This UI does not claim arbitrary Unicode
editing/rendering just because its underlying text core supports graphemes.

Field presses select that field through existing F6 focus. The large editor band
shows a 36-cell horizontal window and a caret; tapping the band positions it.
Home/Left/Right/End/All/Clear use native edit operations. Shift+Left/Right extends
a selection; a subsequent character replaces it. Shift affects the next letter;
Caps is persistent. ABC/123 changes the character layout. Number/PIN use a digit
layout and disable nonnumeric controls. Long Backspace uses the existing F6 long
press (500 ms) and bounded 85 ms repeats; release, movement, input cancellation,
field change or focus loss stops it. No catch-up burst is replayed.

Next selects the next enabled field. Hide/Show keys hides only the key area, not
the owning overlay or field. The 48px top control always allows reopening.
Confirm returns committed values once to the trusted owner; Cancel returns none.
In Coffee only non-sensitive demo Name/Number survive reopening, in RAM only.
Password/PIN are never copied into Coffee state and are erased with their sessions
on close. Confirm does not save device settings, call network APIs or actuators.

## Reuse and boundary

`hosts/linux/text-input/keyboard.h` borrows the caller's existing tree, components,
layout, overlays and F6 runtime. It supports 1-4 explicitly configured fields and
supplied style references. It creates a POCKET_OVERLAY_KEYBOARD, not a second
navigation, hit-test or focus system. F6 focus gain/loss drives the TextSession
admission latch. Event callbacks update bounded command/press state only; component
updates, focus commands and destruction run after dispatch in keyboard_step.

A key press is bound to the field/session/focus/engine/revision at pointer down.
A later field switch cannot make an old release type into the new field. One
owning key contact and one pending semantic action are admitted at a time; this is
not a multi-thumb chord or hardware-keyboard implementation. The existing upstream
keyboard-touch.ts at 53a17f6416c3333f1171141bf996695720101ed2 was reviewed; its
contact-owned cancellation/bounded-repeat principles are adapted to this native
F6 owner. Its TS helper, space-trackpad and full offload IME controller are not
claimed to be executing here; no dictionary/conversion algorithm was rewritten.

The current glyph-cell renderer reuses the pinned P4 atlas, which already includes
printable ASCII. Text references 1000+ASCII and the English keyboard labels200..226
are registered by assets/locales/keyboard-ascii.json independently of UI locale.
Fixed24px cells make hit/caret/selection geometry explicit; they are not shaping,
BiDi or proportional-text caret support. The existing scene projection admits
TextField (21) with normal clipping and children. No wire version, renderer pin,
font source, framebuffer or touch protocol/calibration is changed.

600 and800 keep the fields/editor/actions above the bottom-anchored keyboard.
The largest form plus existing Home stays below256 semantic components. Idle Home
adds one entry node:8items33nodes/100items45nodes; card pool remains12maximum.
The keyboard's48key cells,36editor cells and8preview cells per field are bounded.
There is no precreation of unbounded text or a runtime font download.

## Privacy and failure handling

No key values, committed text, preedit, candidates or text hashes go into snapshots,
CSV, startup/runtime reports or logs. Password/PIN glyph projection uses only '*'.
The production snapshot API refuses every open editor, not only password mode,
and also refuses stale editor pixels immediately after Cancel/Confirm until a new
safe frame has been rendered. Refusal occurs before opening a file. The ordinary
runner prints FRAMEWORK_SNAPSHOT_SUPPRESSED rather than failing on exit with an
open editor; a missing last.ppm in that case is intentional. Reports contain only
text_input_open/opens/confirms/cancels counters.

Tests use synthetic fixtures and a test-only direct pixel capture helper to verify
layout and masked output. That capture helper is not linked into ui-framework.
There is no diagnostic "allow secrets" flag. This does not prevent a user photographing
the screen, provide process-memory isolation, certify external caller copies, or
implement a clipboard/IME privacy model for future providers.

Invalid initial values, duplicate fields, over-limit edits and stale key tokens
fail without committing drafts. Disabled fields cannot get focus, readonly values
cannot change. External overlay loss/cancellation destroys no unrelated component.
There is no hidden ASCII fallback for a configured unsupported language.

## Validation and use

```sh
make test-keyboard-layouts
make test-framework test-framework-arm test-framework-sanitize verify-framework
make build-board-framework test-board-framework-package test-board-framework-verifier
# Isolated board, matching complete package in a new directory:
./run-framework.sh imx6ul-1024x600 180
```

No confirmation string, guessed input coordinates or fixed event number is needed
on the already admitted Goodix600 target.800 still needs its own admitted orientation.
The complete bundle now links the pinned utf8proc Unicode code and carries its
license. Do not mix an old framework.js/catalog with the new executable.

The full framework tests preserve the existing26images/6replays and add16 synthetic
keyboard images across600/800. Repeats, Native/ARM and sanitizer outputs must agree.
Old Home changes only to expose the new entry; old pixels are not claimed globally
identical to the prior package. Tests cover actual down/up/F6 delivery, editing,
partial selection, field switch, symbols/case, digits, password/PIN masking,
confirm/cancel, stale cross-field release, hide/reopen, input loss, repeat release,
100 owner lifecycles and20 rendered reopen cycles per viewport. Wrong-grapheme and
wrong-pixel negative gates stay active. Every rendered integration frame retains
the full-vs-damage pixel oracle.

Physical acceptance remains separate: on this package open Text input, enter only
synthetic test values, edit, switch fields, confirm/reopen and cancel/reopen, hide
and show keys, long-delete, and return through Coffee flow. Return original logs
and an actual LCD/finger video using only synthetic data. Do not infer IME or
physical usability from generated screenshots or QEMU execution.
