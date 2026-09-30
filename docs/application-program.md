# Application program boundary

This is a prerequisite slice of #56 runtime/application separation, **not its
completion**. The accepted reference Coffee ELF still includes native Coffee
logic. No existing framework behavior, board package, scene adapter, input
protocol or renderer is replaced by this isolated program core.

## Intended ownership

Application pages, state and event handlers will live in an independent
application payload. The platform retains native framework components,
interaction, keyboard, device host and the private scene adapter. The program
core uses the **same locked QuickJS source/version** already used by P0/P1,
with a separate heap/context for application logic. It does not create a new
JavaScript engine or share raw JS values with the rendering host.

The C header exposes only bounded primitive argument/result values and an
opaque program. Neither JSValue nor renderer-native handles enter it. The
native command callback will be bound to public Pocket semantics by the later
application world. In this slice the command callback is exercised by explicit
test-owned sum/echo/data fixtures, not a fake UI renderer.

## Program contract

The trusted prototype payload is JavaScript source defining
`globalThis.PocketApplication = {start, event, tick, inspect}`. Calls take and
return JSON data. `__pocketCall(operation, ...primitiveArguments)` invokes an
allowlisted native operation callback. Callback data is copied before borrowed
string lifetimes end. Host/application calls cannot reenter the VM or destroy
it while it executes.

Current source loading is an internal development contract; it is not a
finished compiler or a claim that parsing has been removed from the deployed
runtime. R3 owns package/version admission and later toolchain work can produce
compatible precompiled payloads. Never load untrusted QuickJS bytecode simply
because a hash was supplied.

Default limits: 256 KiB source, 8 KiB text/JSON, 32 primitive arguments, 8 MiB
VM memory, 256 KiB stack, 512 cooperative interrupt checks, 2048 native commands
and 128 pending jobs per call. Configuration has explicit upper bounds. Array
objects, fractional/nonfinite/unsafe integers, embedded NUL and opaque native
operations are rejected at the primitive bridge. Native code remains
responsible for each operation's schema, ownership and capability policy.

Methods must return synchronous data. Promise/thenable results are rejected;
fulfilled microtasks may complete within the same bounded call. A bounded
rejection tracker permits a rejection handled in that turn but fails on an
unhandled rejection. Encoding a result shares the same job/command/fuel budget.
A native rejection cannot be swallowed by application `try/catch` to bypass
the failure latch. Future calls after a failed invocation are refused.

The bare QuickJS context has no standard OS/network/shell bindings. This is not
a production security certification: cooperative interrupts are not a hard
wall-clock deadline for every native algorithm, and the process supervisor,
resource permissions and watchdog remain P7 responsibilities. Application
code is still treated as trusted prototype content pending those controls.

Snapshots contain counters and fixed error codes only. Raw exceptions,
application text, preedit and credentials are not written to diagnostics.
`inspect` output is application data for the caller, **not automatically safe
telemetry**; the future world must expose only explicitly safe diagnostics and
retain the existing keyboard screenshot suppression.

## Verification

```sh
make fetch
make -f scripts/application-program.mk test-application-program
make -f scripts/application-program.mk test-application-program-arm
make -f scripts/application-program.mk test-application-program-sanitize
make -f scripts/application-program.mk verify-application-program
```

The isolated tests compile real pinned QuickJS, including instrumenting its
source in the sanitizer build; repeated and Native/ARM results must agree.
They include malformed argument/data, stale failed-instance reuse, budgets,
reentrancy, pending jobs, promise policy and 100 lifecycle cases. An intentional
wrong assertion must fail. Commit/tree/source/dependency/binary/log hashes are
bound in evidence. A review-only dependency copy includes original notices;
this support archive is not a device application package.

`application_separation` and `physical_hardware` remain false in these reports.
Two isolated program-state fixtures do not prove two UI applications. The next
slice must bind the real framework, port the complete Coffee workload, load an
independent second application and prove unchanged Runtime SHA, actual pixels,
interactions, privacy and both viewports before R2 may be declared complete.
