# R6 reviewed checker and board staging

Read r6-board-acceptance.md for the four-run Coffee/input matrix and human checks.
This document clarifies artifact identity, collection order and checker limits.

## Approved bundle, not a copied manifest

The verifier checks actual files beside manifest.json: Runtime bytes, every listed
.pui and the original bundle SHA256SUMS must agree. Keep the complete approved
bundle on the review host. The startup.log from the existing runner must identify
the same Runtime hash, embedded manifest, profile, application and successful exit.
A self-consistent manifest from an unknown source is not a trusted release. Reviewers
must independently pin the Actions run, artifact ID and archive SHA256 first.

Do not relabel a tested build as the later merge commit. The artifact's printed
source_commit is the build identity. If only this external checker changed, an
older reviewed Runtime bundle can be staged under its actual build identity; this
is not a claim that the new commit's Runtime has been independently rerun.

## Gates added during review

The initial verifier accepted a failed/wrong-size display report, a different ELF
hash in startup.log and a symlinked parent directory in synthetic negative probes.
The next revision validates those semantics and includes explicit regression cases.

Display evidence must be a successful writable display-test with the correct
geometry and bounded stride/offset/map. A read-only probe or arbitrary JSON is not
sufficient. PPM dimensions and payload length must match the target; timeline
records must be monotonic and bounded by the report's present count. The display
probe is written before presentation, so its presents=0 is not itself a failure.

All path components reject symlinks, traversal and normalized aliases. Missing
synthetic-input markers, replay counts, boolean counters and non-finite JSON reject.
Coffee and input runs must use the same controller/transform per target. A missing
800 controller or copied 600 calibration cannot be approved by a software fixture.
These checks establish consistency, not authenticity against a dishonest operator.

Self-tests create temporary synthetic fixtures only. They emit
R6_VERIFIER_SELF_TEST_OK with physical=false, not a board approval. The CI artifact
contains the exact checker source, test log and source identities. No real approved
review.json is generated, and no Runtime, renderer, input driver or device service
is modified by this slice.

## Practical collection order

1. Select the complete reviewed Framework artifact and verify its archive SHA256.
   Extract its device/imx6ul-coffee-framework.tar.gz without mixing other versions.
   Keep the original artifact, manifest and SHA256SUMS on the review host.
2. Copy the complete coffee-framework directory to a new approved test directory
   on an isolated 600 board. Preserve the normal UI and recovery procedure. Stop
   only competing UI/display/input consumers; retain device control/network/OTA.
3. Run Coffee and input separately, preserving both complete LOG_DIR directories:

       ./run-framework.sh imx6ul-1024x600 180
       ./run-input.sh imx6ul-1024x600 180

4. Establish the independent 800 controller and transform, then use the same
   bundle and documented explicit touch arguments for both 800 scenarios. Do not
   insert guessed zero transforms simply to satisfy the command syntax.
5. Retain incomplete returns even when media/fault counts remain zero. Use a
   suitably longer Coffee session and approved isolated-lab procedures to exercise
   media updates, rollback and controlled input failures. A 180-second smoke run
   does not automatically prove every recovery case. Never edit counters to pass.
6. Close the input editor to a safe frame before timeout. Use only synthetic data;
   do not collect real credentials or disable screenshot suppression.
7. Keep actual LCD/finger videos, additional stress/resource/fault/restart evidence
   and the original four LOG_DIR trees. A named human reviewer checks the evidence
   and records the exact report/video hashes, then runs the closure verifier.

## Review command

    make -f scripts/r6.mk test-r6-board-verifier
    make -f scripts/r6.mk verify-r6-board REPORT=/path/to/returned-r6 MANIFEST=/path/to/complete-approved-bundle/manifest.json

Do not copy manifest.json alone: the command intentionally requires its original
Runtime, packages and checksums. Missing physical data stays pending; #49 is not
closed by merging this checker. P5 performance, P6 device IPC, signing, recovery,
additional languages and production admission remain independent.
