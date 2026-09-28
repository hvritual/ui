# Pocket UI Engine Contract v1

Issue: #34.

## Boundary

```text
Pocket application / future public SDK
              |
        Pocket Runtime
              |
      Engine Contract v1
        /            \
current renderer   future adapters
              |
     Platform Backends
 Display / Input / Text / Asset
```

The contract is internal. Applications never receive an engine-native pointer,
Linux device path, framebuffer structure, LVGL handle, or engine-specific enum.

## Versioning

- `POCKET_ENGINE_ABI_MAJOR=1`
- `POCKET_ENGINE_ABI_MINOR=0`
- every API/backend table carries its ABI version and struct size;
- incompatible major versions fail admission;
- optional behavior is capability-gated, never inferred from a NULL call alone.

Handles are `{slot,generation}`, not native pointers. Removing an object makes
the old generation stale; stale use must fail explicitly.

## Lifecycle and ownership

Runtime owns the engine context and calls:

```text
open -> tick/render/node/resource operations -> close
```

Borrowed frame pixels are valid only until the next engine mutation. Platform
presentation is synchronous at this contract level. Backend failure returns a
typed status and does not authorize engine code to terminate device-control,
network, OTA or other machine services.

## Capabilities

v1 reserves independent capabilities for frame render, damage, timer deadline,
node tree, layout, invalidation, clip, layer and resources.

The current Pocket renderer adapter advertises only capabilities that it can
provide today: frame render, damage and timer deadline. Node/layout/resource
operations return `POCKET_ENGINE_UNSUPPORTED`; they are not silently emulated.

The fake/reference adapter exercises the full v1 surface and stale-handle rules.
F2 will define the actual Pocket UI Object Model semantics; F1 intentionally
does not invent component/widget behavior.

## Platform backends

Display/Input/Text/Asset are distinct versioned contracts. This prevents
`fbdev`, `evdev`, BSP paths, font implementation or asset storage from
becoming GUI-engine API.

F0's physical result reinforces this boundary: LVGL rendering worked once
physical framebuffer ownership moved back to Pocket. A future LVGL adapter must
therefore consume Pocket platform backends rather than publish or own Linux
device details.

## Acceptance

```sh
make test-engine-contract
make test-engine-adapters
make check-no-engine-leak
make test-engine-contract-arm
```

The ARM target is functional evidence only; physical performance remains P5.
