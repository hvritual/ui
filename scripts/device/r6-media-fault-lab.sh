#!/bin/sh
# R6 isolated-board operator helper: NEVER unbinds drivers, injects evdev,
# stops services, changes the display mode, or approves physical acceptance.
set -eu
umask 077

die() { printf 'R6_LAB_REJECTED: %s\n' "$*" >&2; exit 2; }
usage() {
    cat >&2 <<'HELP'
Usage:
  sh r6-media-fault-lab.sh prepare BUNDLE_DIR SESSION_PARENT imx6ul-1024x600|imx6ul-1024x800
  sh r6-media-fault-lab.sh media   BUNDLE_DIR SESSION_DIR
  R6_APPROVED_FAULT_SOP=LAB-REFERENCE sh r6-media-fault-lab.sh fault BUNDLE_DIR SESSION_DIR

The helper only executes the bundled signed-data media installer. Touchscreen
fault injection is an external, individually approved lab operation.
HELP
    exit 2
}
[ "$#" -ge 1 ] || usage
OP=$1; shift

absolute_dir() {
    [ -d "$1" ] && [ ! -L "$1" ] || die "directory missing or symlink: $1"
    (CDPATH= cd -P -- "$1" && pwd -P) || die "cannot resolve directory"
}
sha() {
    [ -f "$1" ] && [ ! -L "$1" ] || die "missing/non-regular file: $1"
    sha256sum "$1" | awk '{print $1}'
}
event() {
    [ -f "$SESSION/actions.tsv" ] && [ ! -L "$SESSION/actions.tsv" ] ||
        die "missing or symlinked operator receipt"
    printf '%s\t%s\t%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$1" "${2:--}" >> "$SESSION/actions.tsv"
}
ask() {
    [ -t 0 ] || die "operator confirmation requires an interactive terminal"
    printf '\n%s\nType YES to record this operator observation: ' "$1"
    answer=
    IFS= read -r answer || die "operator aborted this phase"
    [ "$answer" = YES ] || die "operator did not confirm this phase"
}
assert_bundle() {
    [ -x "$BASE/mediactl" ] || die "bundled mediactl unavailable"
    for name in "updates/demo.public" "updates/demo-a.zip" "updates/demo-b.zip" \
                "manifest.json" "SHA256SUMS" "ui-framework" "run-framework.sh"; do
        sha "$BASE/$name" >/dev/null
    done
    (cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null) || die "bundle SHA256SUMS failed"
}
field() {
    awk -F '\t' -v key="$1" '$1 == key {print $2}' "$SESSION/provenance.tsv"
}
assert_session() {
    SESSION=$(absolute_dir "$1")
    case "$SESSION/" in "$BASE/"*) die "session must be outside original device bundle";; esac
    [ -f "$SESSION/provenance.tsv" ] && [ ! -L "$SESSION/provenance.tsv" ] || die "missing provenance"
    [ -d "$SESSION/store" ] && [ ! -L "$SESSION/store" ] || die "missing isolated store"
    [ -f "$SESSION/actions.tsv" ] && [ ! -L "$SESSION/actions.tsv" ] || die "missing operator actions file"
    [ "$(field bundle_manifest_sha256)" = "$(sha "$BASE/manifest.json")" ] || die "wrong manifest"
    [ "$(field runtime_sha256)" = "$(sha "$BASE/ui-framework")" ] || die "wrong Runtime"
    [ "$(field mediactl_sha256)" = "$(sha "$BASE/mediactl")" ] || die "wrong mediactl"
    [ "$(field demo_key_sha256)" = "$(sha "$BASE/updates/demo.public")" ] || die "wrong public key"
    [ "$(field demo_a_sha256)" = "$(sha "$BASE/updates/demo-a.zip")" ] || die "wrong A ZIP"
    [ "$(field demo_b_sha256)" = "$(sha "$BASE/updates/demo-b.zip")" ] || die "wrong B ZIP"
}
requested() {
    [ -f "$SESSION/store/current" ] && [ ! -L "$SESSION/store/current" ] || die "store has no current generation"
    id=$(sed -n '1p' "$SESSION/store/current")
    case "$id" in *[!0123456789abcdef]*|'') die "malformed generation";; esac
    [ "${#id}" -eq 64 ] || die "bad generation length"
    printf '%s' "$id"
}
media_action() {
    label=$1; shift
    [ ! -e "$SESSION/$label.out" ] && [ ! -L "$SESSION/$label.out" ] &&
        [ ! -e "$SESSION/$label.status.json" ] && [ ! -L "$SESSION/$label.status.json" ] ||
        die "phase already recorded or unsafe: $label"
    if "$BASE/mediactl" "$@" >"$SESSION/$label.out" 2>&1; then
        cat "$SESSION/$label.out"
    else
        cat "$SESSION/$label.out" >&2
        die "signed media action failed: $label"
    fi
    "$BASE/mediactl" status "$SESSION/store" >"$SESSION/$label.status.json" || die "status failed"
}

case "$OP" in
prepare)
    [ "$#" -eq 3 ] || usage
    BASE=$(absolute_dir "$1")
    PARENT=$(absolute_dir "$2")
    case "$PARENT/" in "$BASE/"*) die "session parent must be outside bundle";; esac
    PROFILE=$3
    case "$PROFILE" in imx6ul-1024x600|imx6ul-1024x800) ;; *) die "unsupported profile";; esac
    assert_bundle
    SESSION="$PARENT/r6-mf-$(date -u '+%Y%m%d-%H%M%S')-$$"
    mkdir -m 700 "$SESSION" || die "cannot create session"
    mkdir -m 700 "$SESSION/store" || die "cannot create test store"
    {
        printf 'schema\tR6MF1\nprofile\t%s\n' "$PROFILE"
        printf 'bundle_manifest_sha256\t%s\n' "$(sha "$BASE/manifest.json")"
        printf 'runtime_sha256\t%s\n' "$(sha "$BASE/ui-framework")"
        printf 'mediactl_sha256\t%s\n' "$(sha "$BASE/mediactl")"
        printf 'demo_key_sha256\t%s\n' "$(sha "$BASE/updates/demo.public")"
        printf 'demo_a_sha256\t%s\n' "$(sha "$BASE/updates/demo-a.zip")"
        printf 'demo_b_sha256\t%s\n' "$(sha "$BASE/updates/demo-b.zip")"
    } > "$SESSION/provenance.tsv"
    : > "$SESSION/actions.tsv"
    printf 'R6_SESSION_DIR=%s\nR6_MEDIA_STORE=%s\n' "$SESSION" "$SESSION/store"
    printf '\nTerminal A: run the original, unchanged board diagnostic:\n'
    printf 'FRAMEWORK_APPLICATION=coffee FRAMEWORK_ITEMS=100 FRAMEWORK_STORE_DIR="%s/store" "%s/run-framework.sh" %s 900\n' "$SESSION" "$BASE" "$PROFILE"
    printf '\nTerminal B, after UI is visibly running:\n'
    printf 'sh r6-media-fault-lab.sh media "%s" "%s"\n' "$BASE" "$SESSION"
    printf 'For controlled fault phases, use the approved lab SOP externally.\n'
    ;;
media)
    [ "$#" -eq 2 ] || usage
    BASE=$(absolute_dir "$1")
    assert_bundle
    assert_session "$2"
    ask "The isolated UI is running with FRAMEWORK_STORE_DIR=$SESSION/store; Home is idle."
    event UI_IDLE_READY
    media_action install-a install "$SESSION/store" "$BASE/updates/demo.public" "$BASE/updates/demo-a.zip"
    A=$(requested); event A_REQUESTED "$A"
    ask "The REAL LCD now visibly shows signed demo A at idle Home. Retain actual LCD video."
    event A_VISIBLE_OPERATOR
    ask "Enter Detail/Modal or real touch-held BUSY state. Keep it BUSY while B is installed."
    event B_BUSY_BEGIN_OPERATOR
    media_action install-b install "$SESSION/store" "$BASE/updates/demo.public" "$BASE/updates/demo-b.zip"
    B=$(requested)
    [ "$A" != "$B" ] || die "A and B use same generation"
    event B_BUSY_REQUESTED "$B"
    sleep 3
    ask "The LCD STILL shows A while busy; B did NOT appear before returning idle."
    event B_BUSY_DEFERRED_OPERATOR
    ask "Return to idle Home. Confirm B is now displayed on the real LCD."
    event B_VISIBLE_OPERATOR
    ask "Enter busy Detail/Modal once more and KEEP BUSY while rollback is requested."
    event ROLLBACK_BUSY_BEGIN_OPERATOR
    media_action rollback rollback "$SESSION/store" "$BASE/updates/demo.public"
    R=$(requested)
    [ "$R" = "$A" ] || die "rollback requested generation is not original A"
    event ROLLBACK_BUSY_REQUESTED "$R"
    sleep 3
    ask "Still busy, B remains on the LCD and A is deferred."
    event ROLLBACK_BUSY_DEFERRED_OPERATOR
    ask "Return to idle Home. Confirm original signed A is visibly restored."
    event A_RESTORED_OPERATOR
    printf 'R6_MEDIA_ACTIONS_RECORDED requested_A=%s requested_B=%s; NOT physical acceptance\n' "$A" "$B"
    printf 'Preserve the original run-framework LOG_DIR and actual LCD/finger video.\n'
    ;;
fault)
    [ "$#" -eq 2 ] || usage
    BASE=$(absolute_dir "$1")
    assert_bundle
    assert_session "$2"
    [ -n "${R6_APPROVED_FAULT_SOP:-}" ] || die "R6_APPROVED_FAULT_SOP reference required"
    case "$R6_APPROVED_FAULT_SOP" in *[!a-zA-Z0-9_.:-]* ) die "invalid SOP reference";; esac
    printf 'No driver unbind/rebind, evdev write, or SYN_DROPPED injection is performed here.\n'
    ask "Isolation, operator approval and external SOP $R6_APPROVED_FAULT_SOP are confirmed."
    event FAULT_SOP_ACK_OPERATOR
    ask "Record baseline LCD/input, then begin the externally approved disconnect/reconnect experiment."
    event DISCONNECT_WINDOW_OPERATOR
    ask "A REAL hardware/controller disconnect AND reconnect happened; correct fresh touches work."
    event RECONNECT_OPERATOR
    ask "Invoke the separately approved lab method for real evdev SYN_DROPPED. Do NOT use a normal tap."
    event SYN_DROPPED_WINDOW_OPERATOR
    ask "A REAL SYN_DROPPED was produced for the UI reader, and input recovered. Retain logs/video."
    event SYN_DROPPED_OPERATOR
    ask "There were no ghost taps, stale focus/capture or unintended Coffee actions afterwards."
    event NO_GHOST_TAP_OPERATOR
    printf 'R6_FAULT_OPERATOR_MARKERS_RECORDED. Real Runtime counters and human-review video required.\n'
    ;;
*) usage;;
esac
