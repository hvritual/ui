#!/bin/sh
# Headless synthetic IPC diagnostic. Only this run's child PIDs are managed.
# No Python, jq, compiler, loader prefix, framebuffer or input device is used.
set -u
umask 077

IPC_BASE=$(CDPATH= cd -P -- "$(dirname -- "$0")" && pwd) || exit 2
IPC_EMULATOR=
IPC_OUTPUT=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --emulator)
            [ "$#" -ge 2 ] && [ -z "$IPC_EMULATOR" ] || exit 2
            IPC_EMULATOR=$2
            shift 2
            ;;
        --output)
            [ "$#" -ge 2 ] && [ -z "$IPC_OUTPUT" ] || exit 2
            IPC_OUTPUT=$2
            shift 2
            ;;
        *)
            echo "usage: ./run-ipc.sh [--output ABSOLUTE_NEW_DIR] [--emulator ABSOLUTE_QEMU_ARM]" >&2
            exit 2
            ;;
    esac
done

for IPC_TOOL in sha256sum mktemp mkdir chmod sleep sed grep find cp rm rmdir date uname id cat; do
    command -v "$IPC_TOOL" >/dev/null 2>&1 || {
        echo "IPC_BOARD_FAILED missing utility: $IPC_TOOL" >&2
        exit 2
    }
done
for IPC_PATH in "$IPC_BASE" "$IPC_OUTPUT" "$IPC_EMULATOR"; do
    case "$IPC_PATH" in *'
'*)
        echo "IPC_BOARD_FAILED paths containing control characters are unsupported" >&2
        exit 2
        ;;
    esac
    if printf '%s' "$IPC_PATH" | LC_ALL=C grep -q '[[:cntrl:]]'; then
        echo "IPC_BOARD_FAILED paths containing control characters are unsupported" >&2
        exit 2
    fi
done
IPC_MODE=board
if [ -n "$IPC_EMULATOR" ]; then
    case "$IPC_EMULATOR" in /*) ;; *) echo "emulator path must be absolute" >&2; exit 2;; esac
    [ -f "$IPC_EMULATOR" ] && [ -x "$IPC_EMULATOR" ] || exit 2
    IPC_MODE=qemu
fi
if [ -n "$IPC_OUTPUT" ]; then
    case "$IPC_OUTPUT" in /*) ;; *) echo "output path must be absolute" >&2; exit 2;; esac
else
    [ ! -L "$IPC_BASE/logs" ] || exit 2
    if [ ! -d "$IPC_BASE/logs" ]; then mkdir "$IPC_BASE/logs" || exit 2; fi
    IPC_OUTPUT="$IPC_BASE/logs/ipc-$(date +%Y%m%d-%H%M%S)-$$"
fi
[ ! -e "$IPC_OUTPUT" ] && [ ! -L "$IPC_OUTPUT" ] || {
    echo "IPC_BOARD_FAILED output already exists: $IPC_OUTPUT" >&2
    exit 2
}
mkdir "$IPC_OUTPUT" || exit 2
IPC_RUN=$(CDPATH= cd -P -- "$IPC_OUTPUT" && pwd) || exit 2
IPC_SOCKET_DIR=
IPC_SOCKET=
IPC_BROKER_PID=
IPC_PROVIDER_PID=
IPC_VENDOR_PID=
IPC_UI_PID=
IPC_BROKER_LOG=
IPC_PROVIDER_LOG=
IPC_VENDOR_LOG=
IPC_UI_LOG=
IPC_MANIFEST_SHA=
IPC_ALL_CASES=0
IPC_FINISHING=0

ipc_note() {
    printf '%s\n' "$*"
    printf '%s\n' "$*" >> "$IPC_RUN/startup.log"
}

ipc_json_string() {
    printf '"'
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
    printf '"'
}

ipc_hash() {
    # stdin avoids GNU sha256sum's filename-escaping prefix for backslash paths.
    IPC_HASH_LINE=$(sha256sum < "$1") || return 1
    read -r IPC_HASH_VALUE IPC_HASH_REST <<EOF
$IPC_HASH_LINE
EOF
}

# Quote each recorded argument so the host verifier can recover the argv even
# when an installation/output path contains spaces or quote characters.
ipc_command() {
    printf '$'
    for IPC_COMMAND_ARG do
        printf " '"
        printf '%s' "$IPC_COMMAND_ARG" | sed "s/'/'\"'\"'/g"
        printf "'"
    done
    printf '\n'
}

# This function backgrounds an executable directly, never a shell function:
# IPC_STARTED_PID is the actual child retained by this shell for wait/cleanup.
ipc_start() {
    IPC_START_ROLE=$1
    IPC_START_LOG=$2
    IPC_START_BINARY=$3
    shift 3
    case "$IPC_START_ROLE" in ui|broker|provider|vendor) ;; *) return 1;; esac
    if [ "$IPC_MODE" = qemu ]; then
        set -- "$IPC_EMULATOR" -cpu cortex-a7 "$IPC_BASE/$IPC_START_BINARY" "$@"
    else
        set -- "$IPC_BASE/$IPC_START_BINARY" "$@"
    fi
    ipc_command "$@" > "$IPC_START_LOG" || return 1
    # A signal between backgrounding and recording $! must not strand a child.
    # Defer exit only for this short launch/adoption interval. These are caught
    # signals (not ignored signals), so exec children retain ordinary semantics.
    IPC_DEFER_EXIT=0
    trap '[ "$IPC_DEFER_EXIT" -ne 0 ] || IPC_DEFER_EXIT=129' HUP
    trap '[ "$IPC_DEFER_EXIT" -ne 0 ] || IPC_DEFER_EXIT=130' INT
    trap '[ "$IPC_DEFER_EXIT" -ne 0 ] || IPC_DEFER_EXIT=143' TERM
    "$@" >> "$IPC_START_LOG" 2>&1 &
    IPC_STARTED_PID=$!
    case "$IPC_START_ROLE" in
        ui) IPC_UI_PID=$IPC_STARTED_PID;;
        broker) IPC_BROKER_PID=$IPC_STARTED_PID;;
        provider) IPC_PROVIDER_PID=$IPC_STARTED_PID;;
        vendor) IPC_VENDOR_PID=$IPC_STARTED_PID;;
    esac
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    [ "$IPC_DEFER_EXIT" -eq 0 ] || exit "$IPC_DEFER_EXIT"
    printf 'process_pid=%s\n' "$IPC_STARTED_PID" >> "$IPC_START_LOG"
}

# All callers pass an unreaped direct child. SIGKILL is confined to that child
# after a bounded grace period; any forced termination fails the collection.
ipc_stop() {
    IPC_STOP_PID=$1
    IPC_STOP_LOG=$2
    IPC_STOP_EXPECT_LIVE=$3
    IPC_STOP_BAD=0
    if kill -0 "$IPC_STOP_PID" 2>/dev/null; then
        kill -TERM "$IPC_STOP_PID" 2>/dev/null || IPC_STOP_BAD=1
        IPC_STOP_WAIT=0
        while kill -0 "$IPC_STOP_PID" 2>/dev/null && [ "$IPC_STOP_WAIT" -lt 5 ]; do
            sleep 1
            IPC_STOP_WAIT=$((IPC_STOP_WAIT + 1))
        done
        if kill -0 "$IPC_STOP_PID" 2>/dev/null; then
            printf 'RUNNER_FORCED_STOP pid=%s\n' "$IPC_STOP_PID" >> "$IPC_STOP_LOG"
            kill -KILL "$IPC_STOP_PID" 2>/dev/null || true
            IPC_STOP_BAD=1
        fi
    elif [ "$IPC_STOP_EXPECT_LIVE" = yes ]; then
        printf 'RUNNER_UNEXPECTED_EARLY_EXIT pid=%s\n' "$IPC_STOP_PID" >> "$IPC_STOP_LOG"
        IPC_STOP_BAD=1
    fi
    wait "$IPC_STOP_PID"
    IPC_STOP_RC=$?
    printf '\nexit=%s\n' "$IPC_STOP_RC" >> "$IPC_STOP_LOG"
    [ "$IPC_STOP_RC" -eq 0 ] && [ "$IPC_STOP_BAD" -eq 0 ]
}

ipc_stop_group() {
    IPC_GROUP_BAD=0
    if [ -n "$IPC_VENDOR_PID" ]; then
        ipc_stop "$IPC_VENDOR_PID" "$IPC_VENDOR_LOG" yes || IPC_GROUP_BAD=1
        IPC_VENDOR_PID=
    fi
    if [ -n "$IPC_PROVIDER_PID" ]; then
        ipc_stop "$IPC_PROVIDER_PID" "$IPC_PROVIDER_LOG" yes || IPC_GROUP_BAD=1
        IPC_PROVIDER_PID=
    fi
    if [ -n "$IPC_BROKER_PID" ]; then
        ipc_stop "$IPC_BROKER_PID" "$IPC_BROKER_LOG" yes || IPC_GROUP_BAD=1
        IPC_BROKER_PID=
    fi
    [ "$IPC_GROUP_BAD" -eq 0 ]
}

ipc_write_result() {
    {
        printf '{\n  "schema": 1,\n  "mode": "%s",\n  "emulator": ' "$IPC_MODE"
        if [ -n "$IPC_EMULATOR" ]; then ipc_json_string "$IPC_EMULATOR"; else printf 'null'; fi
        printf ',\n  "package_directory": '; ipc_json_string "$IPC_BASE"
        printf ',\n  "output_directory": '; ipc_json_string "$IPC_RUN"
        printf ',\n  "completed": %s,\n  "exit_code": %s,\n' "$IPC_COMPLETED" "$IPC_FINAL_RC"
        printf '  "manifest_sha256": "%s",\n' "$IPC_MANIFEST_SHA"
        printf '  "physical_io": false,\n  "business_commands": false,\n'
        printf '  "production_authentication": false\n}\n'
    } > "$IPC_RUN/run.json"
}

ipc_finish() {
    [ "$IPC_FINISHING" -eq 0 ] || return
    IPC_FINISHING=1
    IPC_FINAL_RC=$1
    trap - 0
    # Finish the bounded cleanup once, preserving the first interruption code.
    trap '' HUP INT TERM
    if [ -n "$IPC_UI_PID" ]; then
        ipc_stop "$IPC_UI_PID" "$IPC_UI_LOG" no || true
        IPC_UI_PID=
        [ "$IPC_FINAL_RC" -ne 0 ] || IPC_FINAL_RC=1
    fi
    if ! ipc_stop_group; then
        [ "$IPC_FINAL_RC" -ne 0 ] || IPC_FINAL_RC=1
    fi
    if [ -n "$IPC_SOCKET_DIR" ]; then
        # This directory was freshly created by this run. Never inspect, reuse
        # or remove another run's socket, PID file or evidence directory.
        if [ -S "$IPC_SOCKET" ] && ! rm -f "$IPC_SOCKET"; then
            [ "$IPC_FINAL_RC" -ne 0 ] || IPC_FINAL_RC=1
        fi
        if ! rmdir "$IPC_SOCKET_DIR"; then
            [ "$IPC_FINAL_RC" -ne 0 ] || IPC_FINAL_RC=1
        fi
    fi
    IPC_COMPLETED=false
    if [ "$IPC_FINAL_RC" -eq 0 ] && [ "$IPC_ALL_CASES" -eq 1 ]; then IPC_COMPLETED=true; fi
    if [ "$IPC_COMPLETED" = false ] && [ "$IPC_FINAL_RC" -eq 0 ]; then IPC_FINAL_RC=1; fi
    if ! ipc_write_result; then
        [ "$IPC_FINAL_RC" -ne 0 ] || IPC_FINAL_RC=1
    fi
    if [ "$IPC_FINAL_RC" -eq 0 ]; then
        ipc_note "IPC_BOARD_COLLECTED mode=$IPC_MODE"
    else
        ipc_note "IPC_BOARD_FAILED mode=$IPC_MODE exit=$IPC_FINAL_RC"
    fi
    if ! (cd "$IPC_RUN" && find . -type f ! -path './SHA256SUMS' -exec sha256sum '{}' \;) > "$IPC_RUN/SHA256SUMS"; then
        [ "$IPC_FINAL_RC" -ne 0 ] || IPC_FINAL_RC=1
        IPC_COMPLETED=false
        ipc_write_result || true
        ipc_note "IPC_BOARD_FAILED evidence checksum creation"
        (cd "$IPC_RUN" && find . -type f ! -path './SHA256SUMS' -exec sha256sum '{}' \;) > "$IPC_RUN/SHA256SUMS" || true
    fi
    printf 'LOG_DIR=%s\n' "$IPC_RUN"
    printf '%s\n' "Return this complete directory with the original bundle for host verification."
    exit "$IPC_FINAL_RC"
}

trap 'ipc_finish "$?"' 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

ipc_note "IPC_BOARD_DIAGNOSTIC_BEGIN mode=$IPC_MODE"
ipc_note "Headless synthetic counter only; no LCD/input access or machine service changes."
if ! (cd "$IPC_BASE" && sha256sum -c SHA256SUMS) > "$IPC_RUN/bundle-check.log" 2>&1; then
    ipc_note "IPC_BOARD_FAILED bundle checksum verification"
    exit 1
fi
cp "$IPC_BASE/manifest.json" "$IPC_RUN/bundle-manifest.json" || exit 1
ipc_hash "$IPC_BASE/manifest.json" || exit 1
IPC_MANIFEST_SHA=$IPC_HASH_VALUE
for IPC_PROGRAM in ui-framework pocket-ipcd service-fixture; do
    [ -f "$IPC_BASE/$IPC_PROGRAM" ] && [ -x "$IPC_BASE/$IPC_PROGRAM" ] || exit 1
done
{
    printf 'mode=%s\npackage_directory=%s\noutput_directory=%s\n' "$IPC_MODE" "$IPC_BASE" "$IPC_RUN"
    uname -a
    id
    for IPC_INFO in /proc/version /proc/cpuinfo /proc/meminfo; do
        if [ -r "$IPC_INFO" ]; then
            printf '\n%s\n' "$IPC_INFO"
            cat "$IPC_INFO"
        fi
    done
    if [ "$IPC_MODE" = qemu ]; then
        printf '\nemulator=%s\n' "$IPC_EMULATOR"
        sha256sum "$IPC_EMULATOR"
        "$IPC_EMULATOR" --version
    fi
} > "$IPC_RUN/environment.log" 2>&1
if [ "$IPC_MODE" = board ]; then
    case "$(uname -m)" in armv7*) ;; *)
        ipc_note "IPC_BOARD_FAILED board mode requires ARMv7; QEMU must be explicit"
        exit 1
    esac
fi
IPC_UID=$(id -u) || exit 1
case "$IPC_UID" in ''|*[!0-9]*) exit 1;; esac
IPC_SOCKET_DIR=$(mktemp -d /tmp/pocket-ipc.XXXXXX) || exit 1
chmod 700 "$IPC_SOCKET_DIR" || exit 1
IPC_SOCKET="$IPC_SOCKET_DIR/broker.sock"
printf 'profile\trole\tpid\tservice\n' > "$IPC_RUN/process_groups.tsv"

ipc_ready() {
    IPC_READY_PID=$1
    IPC_READY_LOG=$2
    IPC_READY_SERVICE=$3
    IPC_READY_WAIT=0
    while [ "$IPC_READY_WAIT" -lt 15 ]; do
        kill -0 "$IPC_READY_PID" 2>/dev/null || return 1
        if [ "$IPC_READY_SERVICE" = - ]; then
            if [ -S "$IPC_SOCKET" ]; then
                printf 'RUNNER_BROKER_READY socket=%s\n' "$IPC_SOCKET" >> "$IPC_READY_LOG"
                return 0
            fi
        elif grep -q "^SERVICE_READY service=$IPC_READY_SERVICE " "$IPC_READY_LOG"; then
            return 0
        fi
        sleep 1
        IPC_READY_WAIT=$((IPC_READY_WAIT + 1))
    done
    printf 'RUNNER_READINESS_TIMEOUT pid=%s\n' "$IPC_READY_PID" >> "$IPC_READY_LOG"
    return 1
}

ipc_case() {
    IPC_CASE_NAME=$1
    IPC_CASE_PACKAGE=$2
    IPC_CASE_HEIGHT=$3
    IPC_CASE_SELECTION=$4
    IPC_CASE_SECONDS=$5
    IPC_CASE_SOCKET=$6
    IPC_UI_LOG="$IPC_RUN/$IPC_CASE_NAME.log"
    ipc_note "IPC_CASE_BEGIN $IPC_CASE_NAME"
    ipc_start ui "$IPC_UI_LOG" ui-framework --headless --profile "imx6ul-1024x$IPC_CASE_HEIGHT" \
        --package "$IPC_BASE/packages/$IPC_CASE_PACKAGE.pui" --allow-unsigned-package \
        --ipc-development-socket "$IPC_CASE_SOCKET" --seconds "$IPC_CASE_SECONDS" \
        --output "$IPC_RUN/$IPC_CASE_NAME" || return 1
    IPC_CASE_WAIT=0
    while kill -0 "$IPC_UI_PID" 2>/dev/null && [ "$IPC_CASE_WAIT" -lt 90 ]; do
        sleep 1
        IPC_CASE_WAIT=$((IPC_CASE_WAIT + 1))
    done
    if kill -0 "$IPC_UI_PID" 2>/dev/null; then
        printf 'RUNNER_CASE_TIMEOUT pid=%s\n' "$IPC_UI_PID" >> "$IPC_UI_LOG"
        ipc_stop "$IPC_UI_PID" "$IPC_UI_LOG" no || true
        IPC_UI_PID=
        return 1
    fi
    wait "$IPC_UI_PID"
    IPC_CASE_RC=$?
    printf '\nexit=%s\n' "$IPC_CASE_RC" >> "$IPC_UI_LOG"
    IPC_UI_PID=
    [ "$IPC_CASE_RC" -eq 0 ] || return 1
    IPC_CASE_REPORT="$IPC_RUN/$IPC_CASE_NAME/report.json"
    for IPC_CASE_FILE in report.json timeline.csv first.ppm last.ppm; do
        [ -s "$IPC_RUN/$IPC_CASE_NAME/$IPC_CASE_FILE" ] || return 1
    done
    # The C report has a fixed flat JSON format. These checks refuse an
    # incomplete application on the board; the host verifier fully parses and
    # validates JSON, package identities, counters and PPM headers afterwards.
    for IPC_CASE_PATTERN in '"ok":true[,}]' '"error":null[,}]' '"completed":1[,}]' \
        "\"application_selection\":$IPC_CASE_SELECTION[,}]" \
        '"package_admitted":true[,}]' '"ipc_development":true[,}]' \
        '"physical_io":false[,}]' '"visual_validated":false[,}]' \
        '"business_commands":false[,}]' '"package_authenticated":false[,}]' \
        '"core_live_bytes_after_close":0[,}]' '"ipc_pending":0[,}]'; do
        grep -Eq "$IPC_CASE_PATTERN" "$IPC_CASE_REPORT" || return 1
    done
    grep -q '^FRAMEWORK_EXIT ok=1 error=none' "$IPC_UI_LOG" || return 1
    ipc_hash "$IPC_RUN/$IPC_CASE_NAME/first.ppm" || return 1
    IPC_CASE_FIRST=$IPC_HASH_VALUE
    ipc_hash "$IPC_RUN/$IPC_CASE_NAME/last.ppm" || return 1
    [ "$IPC_CASE_FIRST" != "$IPC_HASH_VALUE" ] || return 1
    for IPC_CASE_PEER in "$IPC_BROKER_PID" "$IPC_PROVIDER_PID" "$IPC_VENDOR_PID"; do
        if [ -n "$IPC_CASE_PEER" ]; then kill -0 "$IPC_CASE_PEER" 2>/dev/null || return 1; fi
    done
    ipc_note "IPC_CASE_COLLECTED $IPC_CASE_NAME"
}

for IPC_HEIGHT in 600 800; do
    IPC_PROFILE="imx6ul-1024x$IPC_HEIGHT"
    IPC_BROKER_LOG="$IPC_RUN/broker-$IPC_HEIGHT.log"
    ipc_start broker "$IPC_BROKER_LOG" pocket-ipcd --socket "$IPC_SOCKET" --client-uid "$IPC_UID" --service-uid "$IPC_UID" || exit 1
    printf '%s\tbroker\t%s\t-\n' "$IPC_PROFILE" "$IPC_BROKER_PID" >> "$IPC_RUN/process_groups.tsv"
    ipc_ready "$IPC_BROKER_PID" "$IPC_BROKER_LOG" - || exit 1
    IPC_PROVIDER_LOG="$IPC_RUN/provider-$IPC_HEIGHT.log"
    ipc_start provider "$IPC_PROVIDER_LOG" service-fixture --socket "$IPC_SOCKET" --service demo.counter.v1 || exit 1
    printf '%s\tprovider\t%s\tdemo.counter.v1\n' "$IPC_PROFILE" "$IPC_PROVIDER_PID" >> "$IPC_RUN/process_groups.tsv"
    ipc_ready "$IPC_PROVIDER_PID" "$IPC_PROVIDER_LOG" demo.counter.v1 || exit 1
    ipc_case "example-$IPC_HEIGHT" ipc-example "$IPC_HEIGHT" 3 8 "$IPC_SOCKET" || exit 1

    # The first provider and broker stay alive while a new namespace and four
    # new member names are registered. Neither executable is rebuilt/restarted.
    IPC_VENDOR_LOG="$IPC_RUN/vendor-provider-$IPC_HEIGHT.log"
    ipc_start vendor "$IPC_VENDOR_LOG" service-fixture --socket "$IPC_SOCKET" --service vendor.example.v1 \
        --query read --command increase --event updated --state values || exit 1
    printf '%s\tvendor-provider\t%s\tvendor.example.v1\n' "$IPC_PROFILE" "$IPC_VENDOR_PID" >> "$IPC_RUN/process_groups.tsv"
    ipc_ready "$IPC_VENDOR_PID" "$IPC_VENDOR_LOG" vendor.example.v1 || exit 1
    ipc_case "vendor-$IPC_HEIGHT" ipc-vendor "$IPC_HEIGHT" 3 8 "$IPC_SOCKET" || exit 1
    if [ "$IPC_HEIGHT" -eq 600 ]; then
        for IPC_CHANNEL in query command events state; do
            ipc_case "denied-$IPC_CHANNEL" "denied-$IPC_CHANNEL" 600 10 4 "$IPC_SOCKET" || exit 1
        done
    fi
    ipc_stop_group || exit 1
    [ ! -e "$IPC_SOCKET" ] && [ ! -L "$IPC_SOCKET" ] || exit 1
done
ipc_case offline ipc-offline 600 10 4 "$IPC_SOCKET_DIR/missing.sock" || exit 1
IPC_ALL_CASES=1
exit 0
