#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "${ARTIFACT_ROOT:-}" ]; then
    # shellcheck source=../env.sh
    . "$SCRIPT_DIR/../env.sh"
fi

PERFORMANCE_DIR="$ARTIFACT_ROOT/performance"
BUILD_COMMON="$PERFORMANCE_DIR/build_common.sh"
REPO_ROOT="$(cd "$ARTIFACT_ROOT/../.." && pwd)"
export BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
PERFORMANCE_RESULT_SCRIPT_DIR="$REPO_ROOT/src/result-script/performance"

FUZZER_LIST=(
    "rustgo"
    "afl"
    "aflpp"
    "aflgo"
    "aflrun"
    "windranger"
    "fishfuzz"
    "lyso"
    "panickiller"
)

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

TOTAL_BUILDS=$((${#FUZZER_LIST[@]} * ${#TARGET_LIST[@]}))
CURRENT_BUILD=0

declare -a SUCCEEDED=()
declare -a FAILED=()
declare -a SKIPPED=()

function prepare_fuzzer_result_scripts {
    local fuzzer=$1
    local fuzzer_dir="$PERFORMANCE_DIR/$fuzzer"
    local performance_script="$PERFORMANCE_RESULT_SCRIPT_DIR/$fuzzer-performance.sh"

    mkdir -p "$fuzzer_dir"
    if [[ ! -f "$performance_script" ]]; then
        echo "Missing performance result script: $performance_script" >&2
        return 1
    fi

    cp "$performance_script" "$fuzzer_dir/"
    chmod +x "$fuzzer_dir/$(basename "$performance_script")"

    if [[ "$fuzzer" == "rustgo" ]]; then
        local summarize_script="$PERFORMANCE_RESULT_SCRIPT_DIR/summarize_results.sh"

        if [[ ! -f "$summarize_script" ]]; then
            echo "Missing RustGo summary script: $summarize_script" >&2
            return 1
        fi

        cp "$summarize_script" "$fuzzer_dir/summarize_results.sh"
        chmod +x "$fuzzer_dir/summarize_results.sh"
    fi
}

function do_fuzz {
    local fuzzer=$1
    local target=$2
    local fuzzer_dir="$PERFORMANCE_DIR/$fuzzer"
    local target_src="$fuzzer_dir/$target-src"
    local log_file="$fuzzer_dir/build-$target.log"

    mkdir -p "$fuzzer_dir"

    CURRENT_BUILD=$((CURRENT_BUILD + 1))
    printf '[%03d/%03d] Building %s/%s... ' "$CURRENT_BUILD" "$TOTAL_BUILDS" "$fuzzer" "$target"

    if [[ "${PERFORMANCE_REBUILD:-0}" != "1" ]] && built_artifact_exists "$fuzzer" "$target_src"; then
        echo "SKIPPED (already built)"
        SUCCEEDED+=("$fuzzer/$target")
        SKIPPED+=("$fuzzer/$target")
        return 0
    fi

    if (cd "$fuzzer_dir" && bash "$BUILD_COMMON" "$target" "$fuzzer") >"$log_file" 2>&1; then
        echo "SUCCESS"
        SUCCEEDED+=("$fuzzer/$target")
    else
        echo "FAILED (log: $log_file)"
        FAILED+=("$fuzzer/$target")
        return 1
    fi
}

function built_artifact_exists {
    local fuzzer=$1
    local target_src=$2
    local artifact_name="output"

    [[ -d "$target_src" ]] || return 1

    if [[ "$fuzzer" == "rustgo" ]]; then
        artifact_name="rustgo-target"
    elif [[ "$fuzzer" == "panickiller" ]]; then
        artifact_name="pk-target"
    fi

    find "$target_src" \
        -path '*/.git/*' -prune -o \
        -type f -name "$artifact_name" -perm -111 -print -quit | grep -q .
}

function fuzz_all_targets {
    local fuzzer=$1
    local failures=0
    
    for target in ${TARGET_LIST[@]}; do
        if ! do_fuzz "$fuzzer" "$target"; then
            failures=$((failures + 1))
        fi
    done

    return "$failures"
}

function fuzz_all_fuzzers {
    local failures=0

    for fuzzer in ${FUZZER_LIST[@]}; do
        prepare_fuzzer_result_scripts "$fuzzer" || return 1
        if ! fuzz_all_targets "$fuzzer"; then
            failures=$((failures + 1))
        fi
    done

    return "$failures"
}

function fuzz_all {
    fuzz_all_fuzzers
}

fuzz_all

echo
echo "Build summary"
echo "  Success: ${#SUCCEEDED[@]} / $TOTAL_BUILDS"
echo "  Skipped: ${#SKIPPED[@]} / $TOTAL_BUILDS"
echo "  Failed:  ${#FAILED[@]} / $TOTAL_BUILDS"

if ((${#FAILED[@]} > 0)); then
    echo "  Failed builds: ${FAILED[*]}"
    exit 1
fi
