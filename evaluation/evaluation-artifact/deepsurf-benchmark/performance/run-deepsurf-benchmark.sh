#!/usr/bin/env bash

set -uo pipefail

if [[ -z "${FUZZER_NAME:-}" ]]; then
    if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
        echo "FUZZER_NAME is not set" >&2
        return 1
    fi

    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    FUZZERS=(
        "afl"
        "aflpp"
        "aflgo"
        "aflrun"
        "windranger"
        "fishfuzz"
        "rustgo"
        "lyso"
        "panickiller"
    )
    FAILED_FUZZERS=()

    for fuzzer in "${FUZZERS[@]}"; do
        fuzzer_dir="$SCRIPT_DIR/$fuzzer"
        runner="$fuzzer_dir/$fuzzer-deepsurf-benchmark.sh"

        printf '[%s] Running deepSURF benchmark... ' "$fuzzer"
        if [[ ! -x "$runner" ]]; then
            echo "FAILED (runner not found or not executable: $runner)"
            FAILED_FUZZERS+=("$fuzzer")
            continue
        fi

        (
            cd "$fuzzer_dir" &&
                "./$fuzzer-deepsurf-benchmark.sh"
        )
        rc=$?
        if ((rc == 0)); then
            echo "DONE"
        else
            echo "FAILED (exit: $rc)"
            FAILED_FUZZERS+=("$fuzzer")
        fi
    done

    echo
    echo "deepSURF benchmark summary"
    echo "  Failed: ${#FAILED_FUZZERS[@]} / ${#FUZZERS[@]} (${FAILED_FUZZERS[*]:-none})"

    if ((${#FAILED_FUZZERS[@]} > 0)); then
        exit 1
    fi
    exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FUZZER_DIR="${FUZZER_DIR:-$(pwd)}"
ARTIFACT_ENV="$SCRIPT_DIR/../../env.sh"
RUN_LOG_DIR="$FUZZER_DIR/${FUZZER_NAME}-run-logs"
RESULT_CSV="$FUZZER_DIR/${FUZZER_NAME}_deepsurf_results.csv"
POLL_INTERVAL="${POLL_INTERVAL:-1}"
MAX_SECONDS="${MAX_SECONDS:-0}"
CLEAN_OUT="${CLEAN_OUT:-1}"
RUN_SUBDIR="${RUN_SUBDIR:-harness}"
EVAL_SCRIPT_NAME="${EVAL_SCRIPT_NAME:-eval-${FUZZER_NAME}.sh}"

declare -a CRASHED=()
declare -a FAILED=()
declare -a TIMED_OUT=()

source_artifact_env() {
    if [[ -z "${ARTIFACT_ROOT:-}" || -z "${REPO_ROOT:-}" ]]; then
        if [[ ! -f "$ARTIFACT_ENV" ]]; then
            echo "ARTIFACT_ROOT/REPO_ROOT is not set and env.sh was not found: $ARTIFACT_ENV" >&2
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
    find "$FUZZER_DIR" -maxdepth 1 -type d -name 'bug#*' -printf '%f\n' |
        awk -F'[#-]' '$2 >= 1 && $2 <= 26 { print }' |
        sort -V
}

crash_files() {
    local run_dir=$1

    find "$run_dir" \
        \( -path '*/crashes/id:*' -o -path '*/default/crashes/id:*' \) \
        -type f -print 2>/dev/null | sort
}

crash_count() {
    local run_dir=$1

    crash_files "$run_dir" | wc -l
}

first_crash() {
    local run_dir=$1

    crash_files "$run_dir" | head -n 1
}

stop_process_group() {
    local pid=$1

    kill -TERM "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
    sleep 2
    kill -KILL "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

stop_run_dir_processes() {
    local run_dir=$1

    if command -v pkill >/dev/null 2>&1; then
        pkill -TERM -f "$run_dir/(fuzz-${FUZZER_NAME}.sh|afl-fuzz|output|pk-target)" 2>/dev/null || true
        sleep 1
        pkill -KILL -f "$run_dir/(fuzz-${FUZZER_NAME}.sh|afl-fuzz|output|pk-target)" 2>/dev/null || true
    fi
}

clean_outputs() {
    local run_dir=$1

    stop_run_dir_processes "$run_dir"
    rm -rf "$run_dir/out" "$run_dir/shared/findings"
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
    local run_dir="$FUZZER_DIR/$bug_name/$RUN_SUBDIR"
    local eval_script="$run_dir/$EVAL_SCRIPT_NAME"
    local run_log="$RUN_LOG_DIR/$bug_name.log"
    local pid
    local start_ms
    local elapsed_ms
    local max_ms
    local crashes
    local crash_file
    local rc

    printf '[%02d/%02d] Fuzzing %s/%s... ' "$index" "$total" "$FUZZER_NAME" "$bug_name"

    if [[ ! -d "$run_dir" ]]; then
        echo "FAILED (run directory not found)"
        FAILED+=("$bug_name")
        append_result "$bug_name" "failed" 0 0 "" "$run_log"
        return 1
    fi

    if [[ ! -f "$eval_script" ]]; then
        echo "FAILED (eval script not found)"
        FAILED+=("$bug_name")
        append_result "$bug_name" "failed" 0 0 "" "$run_log"
        return 1
    fi

    if [[ "$CLEAN_OUT" == "1" ]]; then
        clean_outputs "$run_dir"
    fi

    if command -v setsid >/dev/null 2>&1; then
        setsid bash -c 'cd "$1" && AFL_NO_UI=1 bash "$2"' bash "$run_dir" "$eval_script" >"$run_log" 2>&1 &
    else
        (
            cd "$run_dir" &&
                AFL_NO_UI=1 bash "$eval_script"
        ) >"$run_log" 2>&1 &
    fi
    pid=$!
    start_ms=$(date +%s%3N)
    max_ms=$((MAX_SECONDS * 1000))
    rc=1

    while kill -0 "$pid" 2>/dev/null; do
        crashes="$(crash_count "$run_dir")"
        if ((crashes > 0)); then
            elapsed_ms=$(($(date +%s%3N) - start_ms))
            crash_file="$(first_crash "$run_dir")"
            stop_process_group "$pid"
            stop_run_dir_processes "$run_dir"
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
                stop_run_dir_processes "$run_dir"
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
        crashes="$(crash_count "$run_dir")"
        crash_file="$(first_crash "$run_dir")"

        if ((crashes > 0)); then
            stop_run_dir_processes "$run_dir"
            echo "CRASH ($elapsed_ms ms, crashes: $crashes)"
            CRASHED+=("$bug_name")
            append_result "$bug_name" "crash" "$elapsed_ms" "$crashes" "$crash_file" "$run_log"
            rc=0
        else
            stop_run_dir_processes "$run_dir"
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
    echo "No bug# directories found under $FUZZER_DIR" >&2
    exit 1
fi

for i in "${!BUG_LIST[@]}"; do
    run_bug "$((i + 1))" "${#BUG_LIST[@]}" "${BUG_LIST[$i]}" || true
done

echo
echo "Results: $RESULT_CSV"
echo "Logs: $RUN_LOG_DIR"
echo "Crashed: ${#CRASHED[@]}"
echo "Timed out: ${#TIMED_OUT[@]}"
echo "Failed: ${#FAILED[@]}"

if ((${#CRASHED[@]} > 0)); then
    printf '  %s\n' "${CRASHED[@]}"
fi
