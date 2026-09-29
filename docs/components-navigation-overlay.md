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
