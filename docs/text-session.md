# Native text sessions — first functional slice of #12

This is the native editor/composition contract and deterministic headless suite.
It is not a keyboard screenshot, real offline IME, editable Coffee screen or board
input-method acceptance. Do not rebuild the existing Coffee board package merely
for this slice: none of its renderer, pager, input or display files changes.

## Progression decision

On 2026-09-30 the user requested that performance tuning stop and the next
functional node advance. Keep the #55 runnable baseline. #49's independent
800/visual/media/fault evidence stays open; it is not a prerequisite for this
headless work (see #12's Framework Track dependency comment). Further performance
work belongs to P5 after representative input functionality is available.

The existing P1 audit in docs/text-input-audit.md already identifies upstream
createIme and keyboard-touch. They must be adapted, not replaced by a new word
conversion algorithm. This slice supplies the missing native editing state. It
does not duplicate an IME provider, UI navigation or F6's focus tree.

## Ownership and API

`hosts/linux/text-input/session.h` is API version 1. All calls are on the owning
UI thread, as for F6. A session stores one field's committed UTF-8 text and a
separate bounded preedit. TextSession focus is an admission latch driven by its
future F6 owner, not an independent component-focus manager. The future binding
must relay focus loss, navigation, overlay changes and component destruction.

Each session gets a process-unique monotonically allocated ID. Field ID is supplied
by the owner. Every event is bound to field/session/focus-generation/engine-generation/
revision. Every accepted event advances revision (even a no-op key); stale,
reordered or duplicate completions are rejected without changing state. Refocusing
or disposing/recreating the same field cannot revive a previous session token.
Engine/language switching calls engine_reset and invalidates outstanding results.
There is no background worker or event queue yet. The planned queue's 64 events /
64 KiB limits in contracts/text-input.json remain explicitly unimplemented.

Session methods return status, metadata and explicit effects. They do not invoke
user callbacks, execute business actions, log text, write files or manage hardware.
The owner copies only committed text into business state through the explicit
copy_committed getter. copy_preedit is a separate rendering/provider interface.
No snapshot contains text or a hash of text; hashing a password is not redaction.

## Unicode boundaries and field policy

`toolchains/text-input.lock.json` pins utf8proc 2.11.3 to commit
`e5e799221b45bbb90f5fdc5c69b6b8dfbf017e78`, its four file Git blob hashes, Unicode
17.0.0 and the independent Unicode GraphemeBreakTest data hash. Build downloads
are bounded and hash-verified; cached corruption is an error, never silently
replaced. Runtime requires no network or external Unicode service. Preserve the
upstream MIT/Unicode LICENSE.md when distributing the linked library.

Committed caret and deletion units are extended grapheme clusters. Convert
explicitly between UTF-8 byte offsets, UTF-16 code units and grapheme indices;
only complete grapheme boundaries are admitted. A surrogate interior, UTF-8
continuation position or interior combining character is rejected, not rounded.
The official Unicode test corpus tests the primitive separately from field policy,
including control/NUL cases. Single-line editable fields reject NUL/CR/LF.

Plain mode preserves all other valid scalar sequences as received. ASCII mode
accepts printable ASCII; PIN mode accepts only ASCII digits and always masks the
value. Byte and grapheme limits are separate. No Unicode normalization, trimming,
case-folding or locale-dependent numeric conversion is performed. Insertion may
join a neighbor's grapheme; limits and caret placement use the whole resulting
string, not the isolated insertion's character count. Initial text follows the
same policy and limits as edits. Rejected edits preserve text, selection, preedit
and every identity counter.

Grapheme segmentation is NOT shaping, BiDi, visual caret positioning or a language
conversion engine. Correct deletion of a combined Korean grapheme does not mean
Korean IME-specific stepwise composition/backspace has been implemented.

## Composition and Enter

Begin fixes the committed selection. Update changes preedit only. Commit replaces
that selection exactly once and clears preedit; cancel/blur/engine reset preserve
committed text and erase preedit. Direct editing or cursor changes during active
composition are rejected so an IME can handle its own composition editing rules.
Enter during composition emits ACCEPT_CANDIDATE, not SUBMIT. A subsequent explicit
composition commit emits COMMITTED, never SUBMIT. A later distinct Enter in the
non-composing state can request submission. No engine or form callback is invoked.

Read-only fields permit selection/navigation, not mutation or submission. Disabled
fields cannot acquire text focus. Changing flags cancels preedit. Sensitive fields
disallow composition; display output is masked by grapheme count. Committed and
preedit storage is overwritten on deletion/replacement/disposal where owned by
this library. Caller copies, screenshot suppression, platform clipboard and
production secret handling still require integration review; they are not certified
by this core-only slice.

## Language capability matrix

`assets/locales/input-capabilities.json` keeps display locale, input locale and
keyboard layout separate. It records all original language groups, layout
proposals, provider/dictionary/shaping state and sensitive policies. No product
input locale is admitted yet. The matrix checker rejects missing rows, fabricated
product admission and unsafe privacy settings. `verify-language-packs` currently
checks this matrix, NOT real dictionaries or keyboard packages.

## Checks

```sh
make test-text-input
make test-text-input-arm
make test-text-input-sanitize
make verify-language-packs
make verify-text-input
```

The script builds the actual core and pinned Unicode source, runs seven test
groups and all pinned Unicode grapheme cases, then repeats the result. Native,
static ARMv7/Cortex-A7 and fully instrumented core+Unicode must agree. A deliberately
wrong grapheme expectation must fail. Other tests cover surrogate/combining/emoji
boundaries, invalid UTF-8, selection replacement, byte/grapheme limits, composition
isolation, double-commit fencing, 1000 focus transitions, engine restart, field
separation, sensitive masking and invalid language matrices. Artifacts bind source,
external dependencies, binary and logs. They contain synthetic fixtures only.

## Next functional slices — no new performance work

1. F6/Component owner binding, dynamic editable text projection and reusable basic
   Latin/number/password touch keyboard; verify both viewports without covering
   the active field. Reuse upstream keyboard-touch behavior where applicable.
2. Adapt upstream createIme to a selected genuine offline provider, bounded
   candidate queue and candidate bar. No fixed candidate stub may pass Chinese
   conversion acceptance; provider/dictionary licensing and BSP build get an ADR.
3. Remaining layouts/languages, shaping/BiDi, sensitive sampling guards and
   per-target physical input acceptance. Only then freeze P5's input workloads.
