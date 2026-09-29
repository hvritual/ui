# Pocket Layout / Style / Theme v1

Issue: #36. Depends on Object Model #35 and Engine Contract #34.

## Layout semantics

Pocket owns six layout modes: Row, Column, Stack, Grid, Absolute and Leaf.
Lengths are a bounded set: px, percent (basis points), content, fill and auto.
The runtime supports min/max constraints, margin/padding/gap, grow/shrink,
wrapping, aspect ratio, safe-area insets and overflow clipping.

This is intentionally not CSS. There are no selectors, media queries, DOM
inheritance rules or vendor properties.

Layout works above the Engine contract. It writes final geometry into
`PocketUiProperties.geometry`; engine adapters only receive the resolved box.

## Determinism and resource bounds

- max 256 direct children per layout container;
- grid columns 1..16;
- percent values are integer basis points;
- integer arithmetic is used for geometry;
- layout records are fixed-capacity after context creation;
- content measurement is an injected Text/Asset-neutral callback;
- application creation order is normalized independently from Object Model's
  internal sibling storage order.

## Style and theme

Style has exactly 11 v1 fields: background, foreground, border color/width,
radius, font ID/size, spacing, opacity and translation X/Y.

Values are literal integers or theme-token references. Cascade is:

```text
active theme base
  -> inherited parent fields (foreground/font-id/font-size only)
  -> component rules matching styleRef/state
     sorted by explicit priority, then registration order
```

States are pressed/focused/disabled/checked/selected. There is no implicit
selector specificity. A missing token fails resolution instead of silently
falling back.

Theme switching only changes active theme + epoch. UI Object handles and
styleRef values are unchanged; business components are not rebuilt.

## Dual viewport golden

The committed 1024×600 and 1024×800 golden fixtures use the same semantic tree,
but independent safe-area inputs and fill calculations. Header/footer retain
fixed heights while the body expands from 376 to 564 px. The 800 profile is
therefore not accepted as a scalar transform of the 600 profile.

## Engine boundary

No schema or public semantic refers to LVGL selector IDs, Flex/Grid enums,
fbdev, evdev or native engine handles. A future LVGL adapter translates these
Pocket semantics privately.


Style runtime rule capacity is explicitly capped at 512 in v1. Resolution uses
a fixed bounded rule pointer array and performs no allocation. Larger rule sets
must be rejected at runtime creation rather than accepted and later failing
during style resolution.


Flex allocation re-applies each child's min/max after grow/shrink. Stretch also
clamps to min/max. An aspect ratio on an auto/content/fill cross axis is
recomputed after main-axis flex sizing, so flexible growth cannot silently break
the declared ratio.

The JSON schema distinguishes size lengths from offsets: negative px is rejected
for width/height but remains valid for absolute offsets, matching the C contract.


Layout records are generation-scoped and stale records are recycled when the
Object Model reports that their handle generation no longer exists. A long-lived
Layout Context therefore has bounded memory even when pages repeatedly
create/destroy nodes. Querying the layout result of a destroyed handle returns
`POCKET_UI_STALE_HANDLE`; historical geometry is never exposed as current.


Resolved style values are field-validated. Colors are unsigned 32-bit values;
opacity is 0..256; border/radius/font/spacing values are non-negative 32-bit
integers; translations are signed 32-bit. Token references are validated after
theme lookup with the same rules, so a valid token ID cannot inject an invalid
field value.


Absolute offsets accept only px or percent. Auto/content/fill are sizing
concepts, not positioning semantics, and are rejected by both Runtime and JSON
Schema.

The style schema now mirrors Runtime field ranges instead of using one generic
integer atom for every field.


Acceptance coverage explicitly exercises percentage width/height, oversized
content measurement under clipped overflow, invalid safe-area geometry, checked
and selected states, and signed translation transforms. These are gates rather
than documentation-only capabilities.
