# Pocket Standard Components / Navigation / Overlay v1

Issue: #37. Depends on Object Model #35 and Layout/Style/Theme #36.

## Components

The first official semantic component set is:

View, Text, Image, Icon, Button, IconButton, Toggle, Checkbox, Radio, Slider,
Progress, Spinner, Scroll, List, Grid, Modal, Dialog, Toast, Loading,
ErrorState and TextField.

Each component owns one Pocket UI semantic root and maps to Pocket layout/style
semantics. No component API exposes LVGL objects, selectors or style properties.

TextField freezes only the contract surface (input kind, text session ID,
max length, read-only/secure state, focus request and submit events). Editing,
composition, candidate handling and IME remain P4A #12.

## Navigation

NavigationStack is bounded and supports push/pop/replace/reset/back. A removed
page runs all registered timer/task/input-capture cancel callbacks exactly once,
then owner cleanup, then optional owned-root destruction. This gives page
teardown one deterministic authority.

Lifecycle callbacks are explicit WILL/DID APPEAR/DISAPPEAR plus DESTROYED.

## Overlay

OverlayManager owns deterministic layer ordering:

popup < toast < modal/dialog/loading < keyboard < IME candidate.

Each overlay can independently capture input, capture focus and consume/dismiss
Back. Owned overlays are destroyed when dismissed. Route-owned overlays can be
bulk-dismissed from Navigation owner cleanup, preventing a Making-page loading
surface from surviving a route replacement.

## Coffee reference flow

The F4 migration fixture builds Home -> Drink Detail -> Making -> Success using
only official component kinds. Making owns a Loading overlay; replacing Making
with Success automatically removes it. Back from Success returns to Home.

This fixture is a semantic migration proof, not the final visual rewrite of P4
Coffee assets.


## Mutation safety

Navigation structural operations are non-reentrant. While push/pop/replace/reset
is invoking lifecycle, resource-cancel or owner-cleanup callbacks, another
structural navigation request returns `POCKET_NAV_BUSY`. This prevents a timer
or input-capture cancellation callback from mutating the same page array while
the current page is being torn down.

Overlay ordering uses a 64-bit monotonic sequence combined with the semantic
layer. Same-layer ordering therefore does not wrap after hundreds of
present/dismiss cycles. Removing a lower focus-capturing overlay also repairs
the saved focus chain of any upper overlay, so later dismissal cannot restore a
focus token owned by an already-destroyed surface.


## Semantic root invariants

A navigation page must be a top-level `View` component. This prevents a child
control or overlay surface from being accidentally promoted into the page
ownership stack.

An owned overlay root must also be top-level. Modal, Dialog, Toast and Loading
overlay kinds require their matching semantic component kind. Modal/Dialog/
Loading overlays additionally require background input capture; a semantic
modal is never allowed to become click-through by configuration.

Grid maps to a container node with Grid layout semantics, not to Scroll. Scroll
and List remain the explicitly scroll-oriented component kinds.


## Dynamic component properties

Applications do not need to bypass the component layer to refresh common UI
content. F4 provides bounded setters for styleRef, textRef, resourceRef,
visibility and disabled state in addition to semantic state/value updates.
Setters update the component record and drive the corresponding Object Model
dirty semantics. Text/image changes therefore stay inside Pocket component
contracts rather than exposing Engine-native objects.
