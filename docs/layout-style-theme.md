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
