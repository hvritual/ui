# R6 dual-board functional closure

R6 closes the physical-function gap in #49 after R5. It is narrower than performance, release/recovery, long-soak, market or production certification. QEMU, headless replay, generated PPM images and process startup never count as physical acceptance.

## Fixed baseline

Use one complete board package built from one accepted source commit. Do not mix an ELF from one artifact with assets, application packages, font, dictionary, scripts or manifest from another build.

The package contains ui-framework, run-framework.sh, run-input.sh, packages/coffee-ime.pui, immutable resources, manifest.json and SHA256SUMS. run-input.sh is an unsigned development diagnostic; success does not imply publisher authentication.

## Required matrix

R6 requires four independent returned runs:

| Target | Coffee | Input / IME |
|---|---|---|
| imx6ul-1024x600 | required | required |
| imx6ul-1024x800 | required | required |

Each run must use a new directory produced by the provided script. Do not edit original JSON, CSV, PPM or SHA256SUMS files.

### 600 target

On an isolated lab machine, stop only competing UI/display/input consumers. Keep device control, network middleware and OTA services untouched.

    ./run-framework.sh imx6ul-1024x600 180
    ./run-input.sh     imx6ul-1024x600 180

Automatic input diagnostics deliberately retain orientation_verified=false; orientation is accepted only by external edge/asymmetric-touch human review.

### 800 target

The 800 target never inherits the 600 controller or transform. Use values proved on that board:

    ./run-framework.sh imx6ul-1024x800 180 --touch-name '<reviewed-controller-name>' --swap-xy 0 --invert-x 0 --invert-y 0
    ./run-input.sh     imx6ul-1024x800 180 --touch-name '<reviewed-controller-name>' --swap-xy 0 --invert-x 0 --invert-y 0

The transform values above are examples, not defaults. The verifier rejects auto as the 800 controller binding.

## Coffee physical sequence

Each board must visibly exercise:

1. Home → Detail → confirmation Modal → simulated Making → Success → Home.
2. Background taps while Modal is open, with no click-through.
3. Cross-card drag, cancel/return, settle and final hit correctness.
4. Large-list virtualization. Use FRAMEWORK_ITEMS=100 in a dedicated run if needed.
5. Locale/theme switching and actual font/image readability.
6. Dynamic image A → B → rollback, including busy deferral and idle application.
7. One controlled input disconnect/reconnect and one controlled SYN_DROPPED lab procedure, followed by no ghost tap or stale capture/focus.

Do not automate kernel/driver reset from this repository. Use the approved lab procedure and never run this test on a customer-serving machine.

For closure, Coffee report.json must record at least one media application and at least one disconnect, reconnect and SYN_DROPPED.

## Text input / IME physical sequence

Use synthetic test values only; never enter real credentials, customer names or Wi-Fi passwords.

On each board:

1. Open Text input.
2. Exercise printable ASCII editing, field switching, confirm and reopen.
3. Exercise cancel/reopen and long Backspace.
4. Switch input locale independently from display locale.
5. Enter synthetic Pinyin, wait for real candidates, page/select a candidate and commit through the normal UI.
6. Verify committed Han text is visible/editable, then confirm.
7. Exercise Password/PIN with synthetic values: no composition, candidates, learning or language chooser.
8. Close the editor before the run ends.
9. Restart after the controlled fault sequence and verify input still works.

The automatic report must show real editor open/confirm/cancel counts, ime_commits >= 1 and ime_candidate_batches >= 1. Reports/logs must contain metadata only, never committed/preedit/candidate text.

## Returned evidence layout

    returned-r6/
    ├── imx6ul-1024x600/
    │   ├── coffee/        # complete LOG_DIR from run-framework.sh + video
    │   ├── input/         # complete LOG_DIR from run-input.sh + video
    │   └── review.json
    └── imx6ul-1024x800/
        ├── coffee/
        ├── input/
        └── review.json

Each scenario must retain startup.log, SHA256SUMS, and runtime/report.json, display.json, input.json, timeline.csv, first.ppm and last.ppm. Videos are added after the script completes and are therefore bound by review.json, not by the run SHA256SUMS.

The input run must close to a safe frame before timeout so last.ppm exists. Use synthetic values only so evidence never captures real secrets.

## Human review

Each profile has one review.json with schema 2. It binds the exact Runtime ELF, both report hashes, every original evidence file and at least one real LCD/finger video for each scenario.

Required checks are exactly:

- lcd_touch
- orientation_edges
- coffee_flow
- modal_no_clickthrough
- drag_cancel_focus
- virtualization
- media_update_rollback
- fault_recovery_no_ghost
- text_input_editing
- pinyin_candidate_commit
- input_locale_switch
- sensitive_field_privacy
- resource_missing_corrupt
- restart_after_fault

review.json also requires reviewer, lab device_id, approved=true, package_binary_sha256, reports.coffee, reports.input and a SHA256 evidence map covering all required files and both videos.

## Validation

First validate the verifier itself:

    make -f scripts/r6.mk test-r6-board-verifier

After both boards return complete evidence:

    make -f scripts/r6.mk verify-r6-board REPORT=/path/to/returned-r6 MANIFEST=/path/to/coffee-framework/manifest.json

A pass means the returned evidence is internally consistent and the complete human-review assertions are present. It is not cryptographic proof that the operator used the stated hardware; human review owns that authenticity.

## Not approved by R6

R6 does not approve P5 performance targets, publisher signing, P6 device/business IPC, P7/P8 release recovery/soak, additional locales beyond the R5 restricted en-US/zh-CN software scope, or market/product/production admission.
