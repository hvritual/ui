# Pocket UI Object Model v1

Issue: #35. Depends on Engine Contract #34.

## Layering

```text
JS / future Pocket SDK
        ↓ opaque PocketUiHandle
Pocket UI Object Model
        ↓ private PocketEngineNode
Engine Contract
        ↓
Current / Fake / future LVGL
```

Applications never receive engine handles. A Pocket UI handle is
`{slot,generation}`; each node also receives a monotonic stable ID for trace,
inspection and semantic references.

## Node kinds

v1 freezes only the minimum semantic categories:

- Node
- Component
- Container
- Text
- Image
- Input
- Scroll
- Custom

These are not widgets and do not define visual style. F3/F4 own layout/style and
standard controls.

## Lifecycle

Normal lifecycle is:

```text
create → mount → update* → layout → paint → unmount → destroy
```

Destroying a mounted parent performs bounded child-first unmount/destroy
cleanup. Engine nodes are removed before the native slot is invalidated.
Failures are reported, but native handles are still invalidated so JS/native
objects cannot remain live after teardown; the Engine owner remains responsible
for final cleanup at engine close.

A child cannot mount before its parent. Paint cannot consume a node while layout
dirty work remains.

## Dirty propagation

- structure: self + ancestor structure/layout/paint
- geometry/visibility/text: self layout/paint + ancestor layout/paint
- style/opacity/semantic-state: self style/paint + ancestor paint
- resource: self resource/paint + ancestor paint
- focusable/clickable/enabled: input dirty; enabled also style/paint

Dirty flags are deduplicated bitsets. Layout and paint explicitly consume their
respective work.

## Events

Events use deterministic:

```text
root capture → ... → parent capture → target → parent bubble → ... → root bubble
```

`CONSUME` stops propagation. `CANCEL` stops propagation and marks
`default_prevented`.

## Bounded update queue

Queued property updates use a fixed configured ring. Enqueue fails with
`POCKET_UI_QUEUE_FULL` instead of growing without bound. Drain is limited by a
per-tick budget and returns `POCKET_UI_BUDGET_EXHAUSTED` while work remains.
An update queued for a destroyed node returns `POCKET_UI_STALE_HANDLE`
deterministically.

## Engine ownership

If an admitted Engine advertises `NODE_TREE`, create/update/destroy operations
also create/update/remove a private Engine node. If no Node Tree capability is
available (for example today's current renderer adapter), the UI Object Model
still operates headlessly. No silent native-engine pointer escapes the model.

## Acceptance

```sh
make test-ui-object-model
make test-ui-lifecycle
make test-ui-dirty
make test-ui-object-arm
```

Object-model stress covers 1,200 create/mount/layout/paint/unmount/destroy
cycles, stable-ID monotonicity, generation invalidation and zero live native
nodes at exit.
