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
        "fishfuzz"
        "lyso"
        "panickiller"
        "windranger"
    )
    FAILED_FUZZERS=()

    for fuzzer in "${FUZZERS[@]}"; do
        fuzzer_dir="$SCRIPT_DIR/$fuzzer"
        runner="$fuzzer_dir/$fuzzer-performance.sh"

        printf '[%s] Running performance benchmark... ' "$fuzzer"
        if [[ ! -x "$runner" ]]; then
            echo "FAILED (runner not found or not executable: $runner)"
            FAILED_FUZZERS+=("$fuzzer")
            continue
        fi

        (
            cd "$fuzzer_dir" &&
                "./$fuzzer-performance.sh"
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
    echo "Performance benchmark summary"
    echo "  Failed: ${#FAILED_FUZZERS[@]} / ${#FUZZERS[@]} (${FAILED_FUZZERS[*]:-none})"

    if ((${#FAILED_FUZZERS[@]} > 0)); then
        exit 1
    fi
    exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FUZZER_DIR="${FUZZER_DIR:-$(pwd)}"
ARTIFACT_ENV="$SCRIPT_DIR/../env.sh"
RESULT_CSV="$FUZZER_DIR/${FUZZER_NAME}_reaching_results.csv"
RUN_LOG_DIR="$FUZZER_DIR/${FUZZER_NAME}-run-logs"
EXPECTED_LINES="${EXPECTED_LINES:-16}"
POLL_INTERVAL="${POLL_INTERVAL:-5}"
MAX_SECONDS="${MAX_SECONDS:-0}"

TARGET_LIST=(
    "nom"
    "json"
    "serde-yaml"
    "unicode-process"
    "unicode-streaming"
    "data-encoding"
    "chrono"
    "mqtt-broker"
    "qcms"
    "httparse"
    "quick-xml"
    "lz4_flex"
    "gimli"
)

declare -a COMPLETED=()
declare -a FAILED=()
declare -a TIMED_OUT=()

source_artifact_env() {
    if [[ -z "${ARTIFACT_ROOT:-}" || -z "${REPO_ROOT:-}" ]]; then
        if [[ ! -f "$ARTIFACT_ENV" ]]; then
            echo "ARTIFACT_ROOT/REPO_ROOT is not set and env.sh was not found: $ARTIFACT_ENV" >&2
            return 1
        fi

        # env.sh prints paths; keep this wrapper output focused on experiment status.
        # shellcheck disable=SC1090
        source "$ARTIFACT_ENV" >/dev/null
    fi
}

find_fuzz_dir() {
    local target=$1
    local target_dir="$FUZZER_DIR/$target-src"

    if [[ ! -d "$target_dir" ]]; then
        return 0
    fi

    find "$target_dir" -name "fuzz-${FUZZER_NAME}.sh" -type f -printf '%h\n' | sort | head -n 1
}

line_count() {
    local file=$1

    if [[ ! -f "$file" ]]; then
        echo 0
        return
    fi

    wc -l < "$file"
}

stop_process_group() {
    local pid=$1

    kill -TERM "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
    sleep 2
    kill -KILL "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

append_reaching_result() {
    local target=$1
    local reaching_log=$2

    awk -v program="$target" '
        NR == 1 {
            start = $1
            next
        }
        NF {
            line = $0
            reach = line
            sub(/^.*: */, "", reach)
            sub(/ .*/, "", reach)

            elapsed = ""
            if (start ~ /^[0-9]+$/ && reach ~ /^[0-9]+$/) {
                elapsed = reach - start
            }

            if (elapsed ~ /^[0-9]+$/) {
                sum += elapsed
                count += 1
            }
        }
        END {
            if (count > 0) {
                printf "%s,%.3f\n", program, sum / count
            } else {
                print program ","
            }
        }
    ' "$reaching_log" >> "$RESULT_CSV"
}

run_target() {
    local index=$1
    local target=$2
    local fuzz_dir
    local fuzz_script
    local reaching_log
    local run_log
    local pid
    local start_seconds
    local elapsed_seconds
    local lines
    local rc

    fuzz_dir="$(find_fuzz_dir "$target")"
    printf '[%02d/%02d] Running %s/%s... ' "$index" "${#TARGET_LIST[@]}" "$FUZZER_NAME" "$target"

    if [[ -z "$fuzz_dir" ]]; then
        echo "FAILED (fuzz-${FUZZER_NAME}.sh not found)"
        FAILED+=("$target")
        return 1
    fi

    fuzz_script="$fuzz_dir/fuzz-${FUZZER_NAME}.sh"
    reaching_log="$fuzz_dir/reaching-log.txt"
    run_log="$RUN_LOG_DIR/$target.log"
    rm -f "$reaching_log"

    if command -v setsid >/dev/null 2>&1; then
        setsid bash -c 'cd "$1" && AFL_NO_UI=1 bash "$2"' bash "$fuzz_dir" "$fuzz_script" >"$run_log" 2>&1 &
    else
        (
            cd "$fuzz_dir" && rm -rf out &&
                AFL_NO_UI=1 bash "$fuzz_script"
        ) >"$run_log" 2>&1 &
    fi
    pid=$!

    start_seconds=$(date +%s)
    rc=1

    while kill -0 "$pid" 2>/dev/null; do
        lines="$(line_count "$reaching_log")"

        if ((lines >= EXPECTED_LINES)); then
            stop_process_group "$pid"
            append_reaching_result "$target" "$reaching_log"
            echo "COMPLETED"
            COMPLETED+=("$target")
            rc=0
            break
        fi

        if ((MAX_SECONDS > 0)); then
            elapsed_seconds=$(($(date +%s) - start_seconds))
            if ((elapsed_seconds >= MAX_SECONDS)); then
                stop_process_group "$pid"
                echo "TIMEOUT ($lines/$EXPECTED_LINES lines, log: $run_log)"
                TIMED_OUT+=("$target")
                rc=1
                break
            fi
        fi

        sleep "$POLL_INTERVAL"
    done

    if ((rc != 0)) && ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid"
        rc=$?
        lines="$(line_count "$reaching_log")"
        if ((lines >= EXPECTED_LINES)); then
            append_reaching_result "$target" "$reaching_log"
            echo "COMPLETED"
            COMPLETED+=("$target")
            rc=0
        else
            echo "FAILED ($lines/$EXPECTED_LINES lines, exit: $rc, log: $run_log)"
            FAILED+=("$target")
        fi
    fi

    return "$rc"
}

source_artifact_env || exit 1
mkdir -p "$RUN_LOG_DIR"

echo "program,avg_elapsed_ms" > "$RESULT_CSV"

for i in "${!TARGET_LIST[@]}"; do
    run_target "$((i + 1))" "${TARGET_LIST[$i]}" || true
done

echo
echo "Experiment summary"
echo "  Completed: ${#COMPLETED[@]} / ${#TARGET_LIST[@]} (${COMPLETED[*]:-none})"
echo "  Timed out: ${#TIMED_OUT[@]} / ${#TARGET_LIST[@]} (${TIMED_OUT[*]:-none})"
echo "  Failed:    ${#FAILED[@]} / ${#TARGET_LIST[@]} (${FAILED[*]:-none})"
echo "  Results:   $RESULT_CSV"
echo "  Logs:      $RUN_LOG_DIR"

if ((${#FAILED[@]} > 0 || ${#TIMED_OUT[@]} > 0)); then
    exit 1
fi
