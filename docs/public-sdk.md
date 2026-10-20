# Public Pocket UI SDK — F7-01 experimental

Scope: #40 / #86. A public JavaScript facade and declaration-only TypeScript types reuse the existing .pui loader. This is NOT a new QML parser, compiler, TypeScript runtime, native plugin loader or publisher-authenticated SDK 1.0.

## Developer surface

- sdk/ui/js/pocket-ui.js: owns the __pocketCall bridge; application code never invokes native operations or imports renderer internals.
- sdk/ui/types/index.d.ts: declaration surface for IDE/TypeScript checking; .ts source does not run on the current target.
- sdk/ui/public-contract.json: constrained machine-readable versions, capabilities and non-goals.
- apps/sdk-demo/application.js: independent counter UI using only PocketUI.
- apps/sdk-demo/manifest.json: existing .pui v1 manifest with ui.core only.

Supported in this facade: View, Text, Button, bounded absolute rectangles, theme tokens, basic style rules, navigation push/replace/pop, catalog text ref mutation, layout invalidation, a queued next-tick tap action and synchronous application lifecycle. Text means a catalog index, not arbitrary dynamic text. New custom drawing, animation, full Reactive API, standalone IME UI and unified IPC wrapper are outside this first slice. Use the existing separate PocketComm JS SDK for Query, Command, Event and State.

PocketUI.define({...}) installs the canonical PocketApplication start/event/tick/inspect lifecycle. F6 action notifications are ONLY queued in event(), with an upper bound of 32, and executed in the following owner tick. There is no synchronous native UI tree mutation inside an event callback. Buttons bind explicit 1..4095 application action IDs. Handles are opaque and invalid outside their app instance. Both target profiles report width=1024 with height=600 or 800. No assumptions are made about framebuffer/device/input controller.

## Build/check

From a checked-out repository with Python 3 and Node:

    python3 scripts/public_sdk.py check
    python3 -m unittest discover -s tests/sdk -p 'test_*.py' -v
    node tests/sdk/test_public_ui.cjs

After the canonical full Framework resources are built:

    python3 scripts/public_sdk.py pack --assets out/framework/applications/control-panel --output out/public-sdk/sdk-example.pui

The pack step uses existing pui.py, copies only approved catalog, glyph atlas, images and notices from an established Framework asset bundle, and overrides only the application.js member with the facade plus example. No new native parser or code is linked; existing application resources remain unchanged. Fonts/source/private key must not be copied into the deployed .pui.

Verify the newly packaged demo on both real software runtime executables:

    python3 scripts/public_sdk.py integration --assets out/framework/applications/control-panel --native out/framework/native/ui-framework --arm out/framework/arm/ui-framework --output out/public-sdk/real-test

Integration checks source commit, binary and .pui hashes, executable capabilities, actual first/last framebuffer snapshots, real rendered text and error-free native and ARM headless runs. ARM uses pinned QEMU in CI, not physical touchscreen/display evidence.

## Limitations and boundaries

The loader's format_version=1, runtime_api=1 and sdk_api=1 are distinct native format/policy fields and remain unchanged. The facade version 0.1-experimental is NOT a claim that public SDK 1.0 or a compatibility support window exists. This example requests ui.core only, a real 2 MiB managed-VM heap and a 16 MiB member budget. Unsigned containers require explicit --allow-unsigned-package in development; production denies them without proper publisher authentication. SHA256 alone does not authenticate a publisher.

The code checks a source-boundary subset to catch mistakes; it is not a security sandbox against a malicious app. The native policy remains authoritative. No existing Coffee/control-panel/IPC application is rewritten or used as substitute test evidence. No device-control, network, OTA, renderer, evdev, IME, firmware or BSP modifications are part of this deliverable.

F7-02A #88 adds the optional [PocketCustom Surface v0](custom-components.md) module and independent bar-chart example. It maps bounded rectangles and axis lines to public Views, with fixed topology and at most 32 primitives per instance. Arbitrary DrawList/Polyline/Path/Arc support requires the separate F7-02B protocol work. F8 #41 owns Simulator/Inspector, #9 owns signing/release, #49 owns independent real 600/800 HIL gates. These remain separate acceptance claims.
