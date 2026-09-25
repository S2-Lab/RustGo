#!/usr/bin/env bash

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARTIFACT_ENV="$SCRIPT_DIR/../../../env.sh"
RUN_LOG_DIR="$SCRIPT_DIR/rustgo-run-logs"
RESULT_CSV="$SCRIPT_DIR/rustgo_deepsurf_results.csv"
POLL_INTERVAL="${POLL_INTERVAL:-1}"
MAX_SECONDS="${MAX_SECONDS:-0}"
CLEAN_OUT="${CLEAN_OUT:-1}"

declare -a CRASHED=()
declare -a FAILED=()
declare -a TIMED_OUT=()

source_artifact_env() {
    if [[ -z "${ARTIFACT_ROOT:-}" ]]; then
        if [[ ! -f "$ARTIFACT_ENV" ]]; then
            echo "ARTIFACT_ROOT is not set and env.sh was not found: $ARTIFACT_ENV" >&2
            return 1
        fi

        # env.sh prints paths; keep this wrapper output focused on fuzzing status.
        # shellcheck disable=SC1090
        source "$ARTIFACT_ENV" >/dev/null
    fi
}

bug_number() {
    local bug_name=$1

    printf '%s\n' "$bug_name" | sed -E 's/^bug#([0-9]+).*/\1/'
}

bug_label() {
    local bug_name=$1

    printf '%s\n' "$bug_name" | sed -E 's/^bug#[0-9]+-//'
}

discover_bugs() {
    find "$SCRIPT_DIR" -maxdepth 1 -type d -name 'bug#*' -printf '%f\n' |
        awk -F'[#-]' '$2 >= 1 && $2 <= 26 { print }' |
        sort -V
}

crash_files() {
    local fuzz_dir=$1

    if [[ ! -d "$fuzz_dir/out" ]]; then
        return 0
    fi

    find "$fuzz_dir/out" -path '*/crashes/*' -type f ! -name README.txt -print | sort
}

crash_count() {
    local fuzz_dir=$1

    crash_files "$fuzz_dir" | wc -l
}

first_crash() {
    local fuzz_dir=$1

    crash_files "$fuzz_dir" | head -n 1
}

stop_process_group() {
    local pid=$1

    kill -TERM "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
    sleep 2
    kill -KILL "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

stop_fuzz_dir_processes() {
    local fuzz_dir=$1

    if command -v pkill >/dev/null 2>&1; then
        pkill -TERM -f "$fuzz_dir/(fuzz-rustgo.sh|rustgo-target)" 2>/dev/null || true
        sleep 1
        pkill -KILL -f "$fuzz_dir/(fuzz-rustgo.sh|rustgo-target)" 2>/dev/null || true
    fi
}

csv_escape() {
    local value=$1

    value=${value//\"/\"\"}
    printf '"%s"' "$value"
}

append_result() {
    local bug_name=$1
    local status=$2
    local elapsed=$3
    local crashes=$4
    local crash_file=$5
    local log_file=$6

    {
        csv_escape "$(bug_number "$bug_name")"
        printf ','
        csv_escape "$bug_name"
        printf ','
        csv_escape "$(bug_label "$bug_name")"
        printf ','
        csv_escape "$status"
        printf ','
        csv_escape "$elapsed"
        printf ','
        csv_escape "$crashes"
        printf ','
        csv_escape "$crash_file"
        printf ','
        csv_escape "$log_file"
        printf '\n'
    } >> "$RESULT_CSV"
}

run_bug() {
    local index=$1
    local total=$2
    local bug_name=$3
    local fuzz_dir="$SCRIPT_DIR/$bug_name/harness/rustgo-fuzz"
    local fuzz_script="$fuzz_dir/fuzz-rustgo.sh"
    local run_log="$RUN_LOG_DIR/$bug_name.log"
    local pid
    local start_ms
    local elapsed_ms
    local max_ms
    local crashes
    local crash_file
    local rc

    printf '[%02d/%02d] Fuzzing rustgo/%s... ' "$index" "$total" "$bug_name"

    if [[ ! -f "$fuzz_script" ]]; then
        echo "FAILED (fuzz script not found)"
        FAILED+=("$bug_name")
        append_result "$bug_name" "failed" 0 0 "" "$run_log"
        return 1
    fi

    if [[ ! -x "$fuzz_dir/rustgo-target" ]]; then
        echo "FAILED (rustgo-target not found or not executable)"
        FAILED+=("$bug_name")
        append_result "$bug_name" "failed" 0 0 "" "$run_log"
        return 1
    fi

    if [[ "$CLEAN_OUT" == "1" ]]; then
        stop_fuzz_dir_processes "$fuzz_dir"
        rm -rf "$fuzz_dir/out"
    fi

    if command -v setsid >/dev/null 2>&1; then
        setsid bash -c 'cd "$1" && AFL_NO_UI=1 bash ./fuzz-rustgo.sh' bash "$fuzz_dir" >"$run_log" 2>&1 &
    else
        (
            cd "$fuzz_dir" &&
                AFL_NO_UI=1 bash ./fuzz-rustgo.sh
        ) >"$run_log" 2>&1 &
    fi
    pid=$!
    start_ms=$(date +%s%3N)
    max_ms=$((MAX_SECONDS * 1000))
    rc=1

    while kill -0 "$pid" 2>/dev/null; do
        crashes="$(crash_count "$fuzz_dir")"
        if ((crashes > 0)); then
            elapsed_ms=$(($(date +%s%3N) - start_ms))
            crash_file="$(first_crash "$fuzz_dir")"
            stop_process_group "$pid"
            stop_fuzz_dir_processes "$fuzz_dir"
            echo "CRASH ($elapsed_ms ms, crashes: $crashes)"
            CRASHED+=("$bug_name")
            append_result "$bug_name" "crash" "$elapsed_ms" "$crashes" "$crash_file" "$run_log"
            rc=0
            break
        fi

        if ((MAX_SECONDS > 0)); then
            elapsed_ms=$(($(date +%s%3N) - start_ms))
            if ((elapsed_ms >= max_ms)); then
                stop_process_group "$pid"
                stop_fuzz_dir_processes "$fuzz_dir"
                echo "TIMEOUT ($elapsed_ms ms, log: $run_log)"
                TIMED_OUT+=("$bug_name")
                append_result "$bug_name" "timeout" "$elapsed_ms" 0 "" "$run_log"
                rc=1
                break
            fi
        fi

        sleep "$POLL_INTERVAL"
    done

    if ((rc != 0)) && ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid"
        rc=$?
        elapsed_ms=$(($(date +%s%3N) - start_ms))
        crashes="$(crash_count "$fuzz_dir")"
        crash_file="$(first_crash "$fuzz_dir")"

        if ((crashes > 0)); then
            stop_fuzz_dir_processes "$fuzz_dir"
            echo "CRASH ($elapsed_ms ms, crashes: $crashes)"
            CRASHED+=("$bug_name")
            append_result "$bug_name" "crash" "$elapsed_ms" "$crashes" "$crash_file" "$run_log"
            rc=0
        else
            stop_fuzz_dir_processes "$fuzz_dir"
            echo "FAILED ($elapsed_ms ms, exit: $rc, log: $run_log)"
            FAILED+=("$bug_name")
            append_result "$bug_name" "failed" "$elapsed_ms" 0 "" "$run_log"
        fi
    fi

    return "$rc"
}

source_artifact_env || exit 1
mkdir -p "$RUN_LOG_DIR"
echo 'bug_number,bug_name,program,status,elapsed_ms,crash_count,first_crash,log_file' > "$RESULT_CSV"

mapfile -t BUG_LIST < <(discover_bugs)

if ((${#BUG_LIST[@]} == 0)); then
    echo "No bug# directories found under $SCRIPT_DIR" >&2
    exit 1
fi

for i in "${!BUG_LIST[@]}"; do
    run_bug "$((i + 1))" "${#BUG_LIST[@]}" "${BUG_LIST[$i]}" || true
done

echo
echo "Experiment summary"
echo "  Crashed:   ${#CRASHED[@]} / ${#BUG_LIST[@]} (${CRASHED[*]:-none})"
echo "  Timed out: ${#TIMED_OUT[@]} / ${#BUG_LIST[@]} (${TIMED_OUT[*]:-none})"
echo "  Failed:    ${#FAILED[@]} / ${#BUG_LIST[@]} (${FAILED[*]:-none})"
echo "  Results:   $RESULT_CSV"
echo "  Logs:      $RUN_LOG_DIR"

if ((${#FAILED[@]} > 0 || ${#TIMED_OUT[@]} > 0)); then
    exit 1
fi
