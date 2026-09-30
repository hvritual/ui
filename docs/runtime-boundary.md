# Runtime and application ownership

This is the first #56 slice: an inventory and a guard on the existing reference
builder. It does **not** extract Coffee, implement an application loader, prove
two-application runtime reuse, or certify a physical board.

## Explicit owners

`contracts/runtime-ownership.json` classifies every admitted translation unit,
object and archive used by `scripts/framework.py` as `runtime`, `application`,
`adapter`, `target`, or `test-only`.

Coffee `app.c` and `pager.c` remain application-owned. The native framework
orchestrator and entry point are reference-only adapters because they still
know the Coffee application. These are visible, limited legacy exceptions,
not permission to add another application's C code to the runtime.

`scene_guest.js` and the generated `out/framework/assets/framework.js` are
private adapter payloads in ownership terms, not developer-editable image or
locale assets. This manifest does not remove the existing generated file or
establish source-code confidentiality. A public repository is still public;
private distribution/access control and package encapsulation are separate
work, not an effect of naming something private.

## Enforced build path

The canonical Make targets invoke `scripts/framework_guard.py`. It calls the
existing builder, dependencies, tests and renderer unchanged, but validates
its actual GCC invocation **before linking**. No substitute renderer is used.

Unknown inputs, added business translation units, test inputs in deployment,
opaque response files/linker options, paths outside the checkout and duplicate
inputs are rejected. GCC `-MM` resolves actual transitive local dependencies;
a platform translation unit importing an application/test header or `.c` file
is rejected. This is stronger than searching only the top-level include lines.

Locked QuickJS/Core/Host/Unicode artifacts are explicitly named and recorded
by hash. Their existing lock/build verification is still required and is not
replaced by ownership classification. Reviewing business logic hidden inside
an already admitted file or archive still requires code/provenance review;
this checker is not a semantic oracle.

The `reference` consumer permits only the enumerated legacy application inputs.
`reference-test` additionally admits named tests. Future `runtime` and
`runtime-test` consumers reject application and reference-only inputs even if
the path is otherwise registered. A test consumer does not make business code
acceptable in a fixed runtime.

After successful compilation, a sibling `*.ownership.json` records the exact
commit/tree, input/dependency hashes, manifest hash and resulting ELF hash.
The report scope is **link-input-ownership-only**. It never sets
`application_separation=true`; that requires the later same-runtime-SHA,
two-independent-interactive-applications proof.

```sh
make check-runtime-boundary
make test-runtime-boundary
make test-framework test-framework-arm test-framework-sanitize verify-framework
make build-board-framework test-board-framework-package
```

The first two commands do not claim UI or board execution. Unit tests use small,
explicit test-owned C fixtures and the real native preprocessor/compiler. Full
Native/ARM/framework/package acceptance remains in the existing workflows.

Directly invoking the old `scripts/framework.py` is no longer a supported
entry point for reference releases; Make routing is checked by CI. Like other
repository checks, this is not protection against an operator bypassing CI or
running an arbitrary compiler command outside the controlled build workflow.

## Next separation proof

Extract application state/pages/behavior into an independent payload, keep the
scene adapter behind the platform boundary, then run the accepted Coffee
workload and a genuinely different application using an unchanged runtime ELF.
Preserve input ownership, overlay cleanup, screenshot suppression and both
viewports. Do not close #56 based on an ownership inventory or a renamed binary.
