# P1 text/input capability audit for P4A (#12)

Audited baseline: `pocket-stack/pocketjs@53a17f6416c3333f1171141bf996695720101ed2`.
The named source file hashes are in `toolchains/runtime.lock.json`. This report
separates source presence, C runtime exposure and actual Linux integration.

| Capability | Evidence in pinned source | Linux Host admission |
|---|---|---|
| Text node mutation | `engine/ui-cabi/include/pocket_ui_cabi.h`: ui_set_text/ui_replace_text; `engine/quickjs-c/pocket_runtime.c` installs setText/replaceText | Real setter calls tested; no glyph/font coverage claim |
| Native focus styling | C ABI ui_set_focus; `framework/src/host.ts` describes focus-style variant | Present, not an editable-field/session manager |
| Keyboard contact behavior | `framework/src/keyboard-touch.ts`: contact-owned holds, repeat/backspace/space-trackpad/cancel | Reusable upstream helper, not integrated into this headless host |
| IME composition/controller | `framework/src/ime.ts`: createIme, preedit/raw/candidate/commit, revision/session fencing; `contracts/spec/ime.ts` contract | Upstream exists. Default provider is offload; no offline provider is linked here |
| Offline dictionary/engine | Above IME controller calls ime.compose/ime.candidates; the C runtime's normal build does not supply them | Not delivered; P4A must adapt a genuine offline provider, not claim raw Latin fallback is Chinese conversion |
| Selection/editor/graphemes | C header exposes label text mutation but no end-to-end editable-widget/selection/composition session API | Not admitted; audit application-level editing before choosing minimum adapters |
| Text resources/shaping | `contracts/spec/text.ts` describes scalar mapping and separates shaped glyph/cluster provider contract | Cannot equate character coverage with shaping/BiDi/caret correctness |

Do not claim "PocketJS has no IME code": it already has reusable composition and
keyboard helpers. Do not claim this Linux host supports IME: the provider and
editable-field path are not connected. Core focus is not text-field focus.
The P1 fixture's Chinese string checks setter/encoding calls only; it loads no
production font and does not certify rendered Chinese or editable text.

## Versioned host boundary

`contracts/text-input.json` records a **future adapter contract**, not implemented
text events. Physical keys, text edits and composition are distinct. Native text
is valid UTF-8; ranges entering JS explicitly use UTF-16 code units. The adapter
must validate conversions and preserve scalar/grapheme boundaries; a Unicode
library/data version is selected and locked in P4A, not guessed here.

Each future event carries field/session/focus-generation/revision identity.
Asynchronous providers deliver on the owning UI thread before the next guest
turn; stale session/revision results are discarded. The planned queue is bounded
at 64 events / 64 KiB, with a 4 KiB text payload limit; these proposed bounds must
be implemented and tested by #12 before admission. Overflow rejects the event and
cancels the composition through an explicit control path, never silently dropping
a commit. Preedit is never a business field value. Confirming a candidate must not
also submit a form. Password policy disables text logging, learning, persistence
and implicit normalization. No new event queue or IME runtime is claimed in P1.

Next: reuse/audit upstream createIme and keyboard-touch, supply a local provider,
implement text sessions/selection and keyboard UI, then verify each admitted
locale on both screen geometries. #12 remains open.
